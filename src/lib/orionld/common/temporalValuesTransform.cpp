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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjString, kjFloat, kjBoolean, kjInteger, kjObject, kjChildAdd
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/temporalValuesTransform.h"              // Own interface



// -----------------------------------------------------------------------------
//
// temporalValuesTransform -
//
// Transform the temporal entity from normalized array format to temporalValues format.
// Each attribute array of instances becomes an array of [value, observedAt] tuples.
//
// Input:  "P1": [ {"type":"Property", "value":30, "observedAt":"..."}, ... ]
// Output: "P1": { "type":"Property", "values": [[30, "..."], [20, "..."], ...] }
//
void temporalValuesTransform(KjNode* entityP)
{
  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    // Skip id, type, scope, and system attributes
    if (attrP->type != KjArray)
      continue;

    // Build the temporalValues object
    KjNode*     valuesArray = kjArray(orionldState.kjsonP, "values");
    const char* attrType    = NULL;

    for (KjNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
    {
      if (instanceP->type != KjObject)
        continue;

      // Extract type (from first instance)
      if (attrType == NULL)
      {
        KjNode* typeP = NULL;
        for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
        {
          if (strcmp(fieldP->name, "type") == 0)
          {
            typeP = fieldP;
            break;
          }
        }
        if (typeP != NULL)
          attrType = typeP->value.s;
      }

      // Find value (or object for Relationship) and observedAt
      KjNode* valueP      = NULL;
      KjNode* observedAtP = NULL;

      for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
      {
        if (strcmp(fieldP->name, "value") == 0 || strcmp(fieldP->name, "object") == 0 || strcmp(fieldP->name, "languageMap") == 0)
          valueP = fieldP;
        else if (strcmp(fieldP->name, "observedAt") == 0)
          observedAtP = fieldP;
      }

      // Build tuple [value, observedAt]
      KjNode* tuple = kjArray(orionldState.kjsonP, NULL);

      if (valueP != NULL)
      {
        // Clone the value node without a name
        KjNode* valClone = NULL;
        switch (valueP->type)
        {
        case KjString:  valClone = kjString(orionldState.kjsonP, NULL, valueP->value.s); break;
        case KjInt:     valClone = kjInteger(orionldState.kjsonP, NULL, valueP->value.i); break;
        case KjFloat:   valClone = kjFloat(orionldState.kjsonP, NULL, valueP->value.f); break;
        case KjBoolean: valClone = kjBoolean(orionldState.kjsonP, NULL, valueP->value.b); break;
        default:        valClone = kjString(orionldState.kjsonP, NULL, ""); break;
        }
        if (valClone != NULL)
          kjChildAdd(tuple, valClone);
      }

      if (observedAtP != NULL)
        kjChildAdd(tuple, kjString(orionldState.kjsonP, NULL, observedAtP->value.s));

      kjChildAdd(valuesArray, tuple);
    }

    // Replace the array of instances with a temporalValues object
    KjNode* tvObj = kjObject(orionldState.kjsonP, attrP->name);
    if (attrType != NULL)
      kjChildAdd(tvObj, kjString(orionldState.kjsonP, "type", attrType));
    kjChildAdd(tvObj, valuesArray);

    // Replace in-place: change attrP to be the object
    attrP->type  = tvObj->type;
    attrP->value = tvObj->value;
  }
}
