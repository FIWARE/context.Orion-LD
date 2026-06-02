/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include <string.h>                                              // strcmp

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjBuilder.h"                                     // kjArray, kjChildAdd, kjChildRemove
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/datasetInstancesMerge.h"                // Own interface



// -----------------------------------------------------------------------------
//
// datasetIdInArray - is 'datasetId' the datasetId of some instance in 'arrayP'?
//
static KjNode* datasetInstanceLookup(KjNode* arrayP, const char* datasetId)
{
  if ((arrayP == NULL) || (arrayP->type != KjArray) || (datasetId == NULL))
    return NULL;

  for (KjNode* instP = arrayP->value.firstChildP; instP != NULL; instP = instP->next)
  {
    KjNode* dsIdP = kjLookup(instP, "datasetId");
    if ((dsIdP != NULL) && (strcmp(dsIdP->value.s, datasetId) == 0))
      return instP;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// datasetInstancesMerge -
//
KjNode* datasetInstancesMerge(KjNode* dbArrayP, KjNode* patchArrayP)
{
  KjNode* mergedArray = kjArray(orionldState.kjsonP, NULL);

  //
  // First pass: copy the DB instances. If a DB instance's datasetId is also in
  // the patch, overlay the patch instance's fields on top of the clone (so the
  // instance is updated, while sub-attributes the patch doesn't mention survive).
  //
  if ((dbArrayP != NULL) && (dbArrayP->type == KjArray))
  {
    for (KjNode* dbInstP = dbArrayP->value.firstChildP; dbInstP != NULL; dbInstP = dbInstP->next)
    {
      KjNode* dbDsIdP = kjLookup(dbInstP, "datasetId");
      KjNode* matchP  = (dbDsIdP != NULL)? datasetInstanceLookup(patchArrayP, dbDsIdP->value.s) : NULL;
      KjNode* cloned  = kjClone(orionldState.kjsonP, dbInstP);

      if (matchP != NULL)
      {
        for (KjNode* patchFieldP = matchP->value.firstChildP; patchFieldP != NULL; patchFieldP = patchFieldP->next)
        {
          KjNode* existingP = kjLookup(cloned, patchFieldP->name);
          if (existingP != NULL)
            kjChildRemove(cloned, existingP);
          kjChildAdd(cloned, kjClone(orionldState.kjsonP, patchFieldP));
        }
      }

      kjChildAdd(mergedArray, cloned);
    }
  }

  //
  // Second pass: append patch instances whose datasetId wasn't in the DB (truly new).
  //
  for (KjNode* newInstP = patchArrayP->value.firstChildP; newInstP != NULL; newInstP = newInstP->next)
  {
    KjNode* newDsIdP = kjLookup(newInstP, "datasetId");

    if ((newDsIdP != NULL) && (datasetInstanceLookup(dbArrayP, newDsIdP->value.s) != NULL))
      continue;  // already handled in the first pass (DB instance updated in place)

    kjChildAdd(mergedArray, kjClone(orionldState.kjsonP, newInstP));
  }

  return mergedArray;
}
