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
#include <postgresql/libpq-fe.h>                               // PGconn

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/context/orionldContextItemExpand.h"          // orionldContextItemExpand
#include "orionld/troe/pgConnectionGet.h"                      // pgConnectionGet
#include "orionld/troe/pgConnectionRelease.h"                  // pgConnectionRelease
#include "orionld/troe/pgTransactionBegin.h"                   // pgTransactionBegin
#include "orionld/troe/pgTransactionRollback.h"                // pgTransactionRollback
#include "orionld/troe/pgTransactionCommit.h"                  // pgTransactionCommit
#include "orionld/troe/pgEntityTreat.h"                        // pgEntityTreat
#include "orionld/troe/troePostEntities.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// troeEntityArrayExpand -
//
void troeEntityArrayExpand(KjNode* tree)
{
  for (KjNode* entityP = tree->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      if (strcmp(attrP->name, "type") == 0)
        attrP->value.s = orionldContextItemExpand(orionldState.contextP, attrP->value.s, true, NULL);
      else if (strcmp(attrP->name, "id")       == 0) {}
      else if (strcmp(attrP->name, "location") == 0) {}
      else
      {
        attrP->name = orionldContextItemExpand(orionldState.contextP, attrP->name, true, NULL);

        if (attrP->type == KjObject)
        {
          for (KjNode* subAttrP = attrP->value.firstChildP; subAttrP != NULL; subAttrP = subAttrP->next)
          {
            if      (strcmp(subAttrP->name, "type")        == 0) {}
            else if (strcmp(subAttrP->name, "id")          == 0) {}
            else if (strcmp(subAttrP->name, "value")       == 0) {}
            else if (strcmp(subAttrP->name, "object")      == 0) {}
            else if (strcmp(subAttrP->name, "observedAt")  == 0) {}
            else if (strcmp(subAttrP->name, "location")    == 0) {}
            else if (strcmp(subAttrP->name, "unitCode")    == 0) {}
            else if (strcmp(subAttrP->name, "datasetId")   == 0) {}
            else
              subAttrP->name = orionldContextItemExpand(orionldState.contextP, subAttrP->name, true, NULL);
          }
        }
      }
    }
  }
}



// ----------------------------------------------------------------------------
//
// troePostBatchCreate -
//
bool troePostBatchCreate(ConnectionInfo* ciP)
{
  PGconn* connectionP;

  //
  // FIXME: the tree should be served expanded + with erroneous entities removed
  //


  // Expanding entity types and attribute names - FIXME: Remove once orionldPostBatchCreate.cpp has been fixed to do that
  troeEntityArrayExpand(orionldState.requestTree);

  connectionP = pgConnectionGet(dbName);
  if (connectionP == NULL)
    LM_RE(false, ("no connection to postgres"));

  if (pgTransactionBegin(connectionP) != true)
    LM_RE(false, ("pgTransactionBegin failed"));

  bool ok = true;
  for (KjNode* entityP = orionldState.requestTree->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    LM_TMP(("TEMP: Calling pgEntityTreat for entity at %p", entityP));
    if (pgEntityTreat(connectionP, entityP, NULL, NULL, orionldState.requestTimeString, orionldState.requestTimeString, TROE_ENTITY_CREATE) == false)
    {
      ok = false;
      break;
    }
  }

  if (ok == false)
  {
    LM_E(("Database Error (batch create TRoE layer failed)"));
    if (pgTransactionRollback(connectionP) == false)
      LM_RE(false, ("pgTransactionRollback failed"));
  }
  else
  {
    if (pgTransactionCommit(connectionP) != true)
      LM_RE(false, ("pgTransactionCommit failed"));
  }

  pgConnectionRelease(connectionP);

  return true;
}