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
#include <cstdlib>                                             // free
#include <string.h>                                            // strncpy

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
#include "orionld/troe/pgAppendInit.h"                         // pgAppendInit
#include "orionld/common/dotForEq.h"                           // dotForEq
#include "orionld/troe/pgAppend.h"                             // pgAppend
#include "orionld/troe/pgAttributesBuild.h"                    // pgAttributesBuild
#include "orionld/troe/pgAttributeBuild.h"                     // pgAttributeBuild
#include "orionld/troe/pgCommands.h"                           // pgCommands
#include "orionld/troe/troeFilterMatch.h"                      // troeFilterMatch
#include "orionld/troe/troePostEntity.h"                       // Own interface



// ----------------------------------------------------------------------------
//
// troePostEntity -
//
bool troePostEntity(void)
{
  if (orionldState.requestTree == NULL)  // Nothing was really changed
    return true;

  if (orionldState.entityTypeForTroe != NULL)
  {
    if (troeFilterMatch(orionldState.entityTypeForTroe, orionldState.wildcard[0]) == false)
    {
      KT_T(KtConfig, "Not storing entities of type '%s' in TRoE - filtered out", orionldState.entityTypeForTroe);
      return true;
    }
  }

  char*           entityId = orionldState.wildcard[0];
  PgAppendBuffer  attributesBuffer;
  PgAppendBuffer  subAttributesBuffer;

  pgAppendInit(&attributesBuffer, 2*1024);     // 2k - enough only for smaller entities - will be reallocated if need be
  pgAppendInit(&subAttributesBuffer, 2*1024);  // ditto

  pgAppend(&attributesBuffer,    PG_ATTRIBUTE_INSERT_START,     0);
  pgAppend(&subAttributesBuffer, PG_SUB_ATTRIBUTE_INSERT_START, 0);

  //
  // Determine per-attribute opMode:
  //   - With noOverwrite: all attributes that reach here are new -> "Append"
  //   - Without noOverwrite and patchBase available: check each attribute
  //     - Existing in DB: "Replace"
  //     - New:            "Append"
  //   - Without noOverwrite and no patchBase: fall back to "Replace"
  //
  if (orionldState.uriParamOptions.noOverwrite == true)
  {
    pgAttributesBuild(&attributesBuffer, orionldState.requestTree, entityId, "Append", &subAttributesBuffer);
  }
  else if (orionldState.patchBase != NULL)
  {
    for (KjNode* attrP = orionldState.requestTree->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      // Skip non-attribute fields
      if (attrP->type != KjObject && attrP->type != KjArray)
        continue;

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
      else
        pgAttributeBuild(&attributesBuffer, opMode, entityId, attrP, &subAttributesBuffer);
    }
  }
  else
  {
    pgAttributesBuild(&attributesBuffer, orionldState.requestTree, entityId, "Replace", &subAttributesBuffer);
  }

  char* sqlV[2];
  int   sqlIx = 0;

  if (attributesBuffer.values    > 0) sqlV[sqlIx++] = attributesBuffer.buf;
  if (subAttributesBuffer.values > 0) sqlV[sqlIx++] = subAttributesBuffer.buf;

  if (sqlIx > 0)
    pgCommands(sqlV, sqlIx);

  if (attributesBuffer.allocated)    free(attributesBuffer.buf);
  if (subAttributesBuffer.allocated) free(subAttributesBuffer.buf);

  return true;
}
