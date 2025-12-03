/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include <unistd.h>                                              // NULL

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjChildAdd
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/common/eqForDot.h"                             // eqForDot



// -----------------------------------------------------------------------------
//
// kjAttributesWithTypeExtract - convert DB entity::attrs field into list of attrs with type
//
// kjTree (incoming):
// {
//   "attrs": {
//     "P1": {
//       "type": "Property",
//       ...
//     },
//     "R1": {
//       "type": "Relationship",
//       ...
//     },
//     ...
//   }
// }
//
// entityP (outgoing):
// {
//   "P1": "Property",
//   "R1": "Relationship"
// }
//
// Also, the '=' in attribute names are to be replaced with '.' (NGSI data model details)
//
bool kjAttributesWithTypeExtract(KjNode* kjTree, KjNode* entityP)
{
  KjNode* attrsP = kjLookup(kjTree, "attrs");

  if (attrsP == NULL)
    return false;

  if (attrsP->type != KjObject)
    return false;

  KjNode* attrP = attrsP->value.firstChildP;
  KjNode* next;

  while (attrP != NULL)
  {
    next = attrP->next;

    KjNode* typeP = kjLookup(attrP, "type");

    if (typeP != NULL)
    {
      // Set 'attrP' to have the value of 'typeP', as that's what we want for 'entityP'
      attrP->value = typeP->value;
      attrP->type  = typeP->type;

      kjChildRemove(attrsP, attrP);
      kjChildAdd(entityP, attrP);

      eqForDot(attrP->name);
    }

    attrP = next;
  }

  return true;
}



