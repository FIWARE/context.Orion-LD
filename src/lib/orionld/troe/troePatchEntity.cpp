/*
*
* Copyright 2020 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Ken Zangelin
*/
#include <string.h>                                            // strncpy, strchr, strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjRender.h"                                    // kjRender
}

#include "orionld/types/PgTableDefinitions.h"                  // PG_ATTRIBUTE_INSERT_START, PG_SUB_ATTRIBUTE_INSERT_START
#include "orionld/types/PgAppendBuffer.h"                      // PgAppendBuffer
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/uuidGenerate.h"                       // uuidGenerate
#include "orionld/troe/pgAppendInit.h"                         // pgAppendInit
#include "orionld/troe/pgAppend.h"                             // pgAppend
#include "orionld/common/dotForEq.h"                           // dotForEq
#include "orionld/common/eqForDot.h"                           // eqForDot
#include "orionld/troe/pgAttributesBuild.h"                    // pgAttributesBuild
#include "orionld/troe/pgAttributeBuild.h"                     // pgAttributeBuild
#include "orionld/troe/pgAttributeAppend.h"                    // pgAttributeAppend
#include "orionld/troe/pgSubAttributeAppend.h"                 // pgSubAttributeAppend
#include "orionld/troe/pgCommands.h"                           // pgCommands
#include "orionld/troe/troeFilterMatch.h"                      // troeFilterMatch
#include "orionld/troe/troePatchEntity.h"                      // Own interface



// ----------------------------------------------------------------------------
//
// troePatchEntity -
//
bool troePatchEntity(void)
{
  char* entityId   = orionldState.wildcard[0];

  if (orionldState.entityTypeForTroe != NULL)
  {
    if (troeFilterMatch(orionldState.entityTypeForTroe, orionldState.wildcard[0]) == false)
    {
      KT_T(KtConfig, "Not storing entities of type '%s' in TRoE - filtered out", orionldState.entityTypeForTroe);
      return true;
    }
  }

  PgAppendBuffer attributesBuffer;
  PgAppendBuffer subAttributesBuffer;

  pgAppendInit(&attributesBuffer, 2*1024);     // 2k - enough only for smaller entities - will be reallocated if necessary
  pgAppendInit(&subAttributesBuffer, 2*1024);  // ditto

  pgAppend(&attributesBuffer,    PG_ATTRIBUTE_INSERT_START,     0);
  pgAppend(&subAttributesBuffer, PG_SUB_ATTRIBUTE_INSERT_START, 0);

  //
  // If patchBase (the DB attributes before modification) is available, determine per-attribute opMode:
  //   - Existing attributes: "Replace"
  //   - New attributes:      "Append"
  // If patchBase is not available, fall back to "Replace" for all attributes.
  //
  if (orionldState.patchBase != NULL)
  {
    for (KjNode* attrP = orionldState.requestTree->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      // Check if this attribute existed in the DB before the modification
      char eqName[512];
      strncpy(eqName, attrP->name, sizeof(eqName) - 1);
      eqName[sizeof(eqName) - 1] = 0;
      dotForEq(eqName);

      KjNode*     dbAttrP = kjLookup(orionldState.patchBase, eqName);
      const char* opMode  = (dbAttrP != NULL) ? "Replace" : "Append";

      if (attrP->type == KjArray)
      {
        for (KjNode* aiP = attrP->value.firstChildP; aiP != NULL; aiP = aiP->next)
        {
          aiP->name = attrP->name;
          pgAttributeBuild(&attributesBuffer, opMode, entityId, aiP, &subAttributesBuffer);
        }
      }
      else if (attrP->type == KjObject)
        pgAttributeBuild(&attributesBuffer, opMode, entityId, attrP, &subAttributesBuffer);
    }
  }
  else
  {
    pgAttributesBuild(&attributesBuffer, orionldState.requestTree, entityId, "Replace", &subAttributesBuffer);
  }

  if (orionldState.patchTree != NULL)
  {
    for (KjNode* patchP = orionldState.patchTree->value.firstChildP; patchP != NULL; patchP = patchP->next)
    {
      KjNode* pathNode = kjLookup(patchP, "PATH");
      KjNode* treeNode = kjLookup(patchP, "TREE");

      if ((treeNode != NULL) && (treeNode->type == KjNull))
      {
        char* attrName = pathNode->value.s;
        char* dotP     = strchr(attrName, '.');

        if (dotP == NULL)
        {
          char instanceId[80];
          uuidGenerate(instanceId, sizeof(instanceId), "urn:ngsi-ld:attribute:instance:");
          pgAttributeAppend(&attributesBuffer, instanceId, attrName, "Delete", entityId, NULL, NULL, true, NULL, NULL, NULL);
        }
        else
        {
          char* subAttrName = &dotP[1];

          *dotP = 0;
          dotP  = strchr(subAttrName, '.');

          if (dotP == NULL)
          {
            if ((strcmp(subAttrName, "value")      != 0) &&
                (strcmp(subAttrName, "unitCode")   != 0) &&
                (strcmp(subAttrName, "observedAt") != 0) &&
                (strcmp(subAttrName, "datasetId")  != 0))
            {
              pgSubAttributeAppend(&subAttributesBuffer, "urn:delete", subAttrName, entityId, "urn:attr-instance:unknown", NULL, "String", NULL, NULL, NULL, NULL);
            }
          }
        }
      }
    }
  }

  char* sqlV[2];
  int   sqlIx = 0;

  if (attributesBuffer.values    > 0) sqlV[sqlIx++] = attributesBuffer.buf;
  if (subAttributesBuffer.values > 0) sqlV[sqlIx++] = subAttributesBuffer.buf;

  if (sqlIx > 0)
    pgCommands(sqlV, sqlIx);

  return true;
}
