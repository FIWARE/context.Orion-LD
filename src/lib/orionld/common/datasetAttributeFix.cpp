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
* Author: Ken Zangelin
*/
#include <string.h>                                              // strcmp

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildRemove
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/datasetAttributeFix.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// datasetIdMatch -
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
// datasetAttributeFix -
//
void datasetAttributeFix(KjNode* entityP, KjNode* attrP)
{
  LM_T(LmtDatasetId, ("Fixing datasetId for attribute '%s'", attrP->name));
  LM_T(LmtDatasetId, ("----------------------------------------------------------------"));

  if (attrP->type == KjArray)
  {
    LM_T(LmtDatasetId, ("It's an array - remove all non-matching instances (or even the entire attribute)"));

    KjNode* attrInstanceP = attrP->value.firstChildP;
    KjNode* next          = NULL;

    while (attrInstanceP != NULL)
    {
      next = attrInstanceP->next;

      KjNode*     datasetIdP = kjLookup(attrInstanceP, "datasetId");
      const char* datasetId  = (datasetIdP != NULL)? datasetIdP->value.s : "@none";

      LM_T(LmtDatasetId, ("This instance has the datasetId '%s'", datasetId));

      if (datasetIdMatch(datasetId) == false)
      {
        // Remove the attribute attribute instance as it doesn't match any datasetId
        LM_T(LmtDatasetId, ("Remove the attribute instance '%s' as it doesn't match any datasetId", datasetId));
        kjChildRemove(attrP, attrInstanceP);
      }
      else
        LM_T(LmtDatasetId, ("Keeping the instance '%s'", datasetId));

      attrInstanceP = next;
    }

    kjTreeLog(attrP, "Attribute after removing non-matching instances", LmtSR);
    if (attrP->value.firstChildP == NULL)
    {
      LM_T(LmtDatasetId, ("No instances left - remove the entire attribute"));
      kjChildRemove(entityP, attrP);
    }
    else if (attrP->value.firstChildP->next == NULL)
    {
      LM_T(LmtDatasetId, ("One single instance left - flatten the array into an object"));
      attrP->value = attrP->value.firstChildP->value;
      attrP->type  = KjObject;
    }
  }
  else if (attrP->type == KjObject)
  {
    LM_T(LmtDatasetId, ("It's an object - keep or remove the entire attribute"));

    KjNode*     datasetIdP = kjLookup(attrP, "datasetId");
    const char* datasetId  = (datasetIdP != NULL)? datasetIdP->value.s : "@none";

    LM_T(LmtDatasetId, ("The datasetId of the attribute instance is '%s'", datasetId));

    if (datasetIdMatch(datasetId) == false)
    {
      // Remove the attribute as it has no instance matching the datasetId
      LM_T(LmtDatasetId, ("Removing the entire attribute '%s'", attrP->name));
      kjChildRemove(entityP, attrP);
    }
    else
      LM_T(LmtDatasetId, ("Keeping matching attribute '%s'", attrP->name));
  }

  LM_T(LmtDatasetId, ("----------------------------------------------------------------"));
}
