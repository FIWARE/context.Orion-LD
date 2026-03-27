/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
* Author: Carsten Frey
*/
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildRemove
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/datasetTemporalEntityFix.h"             // Own interface



// -----------------------------------------------------------------------------
//
// datasetIdMatch - check whether a datasetId matches the requested filter
//
static bool datasetIdMatch(const char* datasetId)
{
  if (orionldState.in.datasetIdList.items > 0)
  {
    for (int ix = 0; ix < orionldState.in.datasetIdList.items; ix++)
    {
      if (strcmp(datasetId, orionldState.in.datasetIdList.array[ix]) == 0)
        return true;
    }
  }
  else if (orionldState.uriParams.datasetId != NULL)
  {
    if (strcmp(datasetId, orionldState.uriParams.datasetId) == 0)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// datasetTemporalEntityFix -
//
// Like datasetEntityFix but for temporal entities.
// Temporal attributes are always arrays of instances - they must NOT be
// flattened to objects even when only one instance remains after filtering.
//
void datasetTemporalEntityFix(KjNode* entityP)
{
  KjNode* attrP = entityP->value.firstChildP;
  KjNode* next  = NULL;

  while (attrP != NULL)
  {
    next = attrP->next;

    // Skip entity-level members (id, type, scope)
    if ((strcmp(attrP->name, "id")    == 0) ||
        (strcmp(attrP->name, "@id")   == 0) ||
        (strcmp(attrP->name, "type")  == 0) ||
        (strcmp(attrP->name, "@type") == 0) ||
        (strcmp(attrP->name, "scope") == 0))
    {
      attrP = next;
      continue;
    }

    // Temporal attributes are arrays of instances
    if (attrP->type != KjArray)
    {
      attrP = next;
      continue;
    }

    // Remove non-matching instances from the array
    KjNode* instanceP = attrP->value.firstChildP;
    KjNode* instanceNext = NULL;

    while (instanceP != NULL)
    {
      instanceNext = instanceP->next;

      KjNode*     datasetIdP = kjLookup(instanceP, "datasetId");
      const char* datasetId  = (datasetIdP != NULL) ? datasetIdP->value.s : "@none";

      if (datasetIdMatch(datasetId) == false)
        kjChildRemove(attrP, instanceP);

      instanceP = instanceNext;
    }

    // If no instances remain, remove the entire attribute
    if (attrP->value.firstChildP == NULL)
      kjChildRemove(entityP, attrP);

    // NOTE: unlike datasetAttributeFix, we do NOT flatten single-instance
    // arrays to objects - temporal representation always uses arrays

    attrP = next;
  }
}
