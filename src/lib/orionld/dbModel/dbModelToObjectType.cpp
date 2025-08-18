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
extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildRemove. kjString, ...
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/numberToDate.h"                         // numberToDate
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/dbModel/dbModelToObjectType.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// dbModelToObjectType -
//
KjNode* dbModelToObjectType(KjNode* dbObjectTypeP)
{
  if (dbObjectTypeP->type == KjObject)
  {
    KjNode* valueP = kjLookup(dbObjectTypeP, "value");

    if (valueP != NULL)
    {
      valueP->name = (char*) "objectType";
      valueP->value.s = orionldContextItemAliasLookup(orionldState.contextP, valueP->value.s, NULL, NULL);
      return valueP;
    }
  }
  else if (dbObjectTypeP->type == KjString)
  {
    dbObjectTypeP->value.s = orionldContextItemAliasLookup(orionldState.contextP, dbObjectTypeP->value.s, NULL, NULL);
    return dbObjectTypeP;
  }

  orionldError(OrionldInternalError, "Database Error", "Invalid objectType in DB", 500);
  return kjString(orionldState.kjsonP, "objectType", "ERROR");
}
