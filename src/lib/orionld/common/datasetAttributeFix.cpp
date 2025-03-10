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

#include "orionld/common/datasetAttributeFix.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// datasetAttributeFix -
//
void datasetAttributeFix(KjNode* entityP, KjNode* attrP, const char* datasetId)
{
  LM_T(LmtDatasetId, ("Fixing datasetId for attribute '%s' (datasetId: '%s'", attrP->name, datasetId));

  if (attrP->type == KjArray)
  {
    LM_T(LmtDatasetId, ("It's an array - remove all non-matching instances (or even the entire attribute)"));
    KjNode* matchP = NULL;

    KjNode* attrInstanceP = attrP->value.firstChildP;
    KjNode* next          = NULL;

    while (attrInstanceP != NULL)
    {
      next = attrInstanceP->next;

      KjNode* datasetIdP = kjLookup(attrInstanceP, "datasetId");
      if (datasetIdP == NULL)
      {
        attrInstanceP = next;
        continue;
      }

      LM_T(LmtDatasetId, ("This instance has the datasetId '%s'", datasetIdP->value.s));
      if (strcmp(datasetIdP->value.s, datasetId) == 0)
      {
        matchP = attrInstanceP;
        break;
      }

      attrInstanceP = next;
    }

    if (matchP == NULL)
    {
      // Remove the attribute as it has no instance matching the datasetId
      LM_T(LmtDatasetId, ("Remove the attribute as it has no instance matching the datasetId"));
      kjChildRemove(entityP, attrP);
    }
    else
    {
      LM_T(LmtDatasetId, ("Keep a single instance of the attribute '%s'", attrP->name));
      // Make the attribute an Object with only this instance
      attrP->value = matchP->value;
      attrP->type  = matchP->type;
      // kjChildRemove(attrP, datasetIdP);
    }
  }
  else if (attrP->type == KjObject)
  {
    LM_T(LmtDatasetId, ("It's an object - keep or remove the entire attribute"));
    KjNode* datasetIdP = kjLookup(attrP, "datasetId");

    if (datasetIdP != NULL)
      LM_T(LmtDatasetId, ("The dataasetId of the attribute instance is '%s'", datasetIdP->value.s));

    if ((datasetIdP == NULL) || (strcmp(datasetIdP->value.s, datasetId) != 0))
    {
      // Remove the attribute as it has no instance matching the datasetId
      LM_T(LmtDatasetId, ("Removing the entire attribute '%s'", attrP->name));

      kjChildRemove(entityP, attrP);
    }
    else
      LM_T(LmtDatasetId, ("Keeping matching attribute '%s'", attrP->name));

    // kjChildRemove(attrP, datasetIdP);
  }
}
