/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjFloat, kjChildAdd
}

#include "logMsg/logMsg.h"                                       // LM*

#include "orionld/common/orionldState.h"                         // orionldState



// -----------------------------------------------------------------------------
//
// dbModelAttributeCreatedAtSet -
//
void dbModelAttributeCreatedAtSet(KjNode* dbAttrP, double createdAt, const char* fieldName)
{
  KjNode* creDateP = kjLookup(dbAttrP, fieldName);  // "creDate" for default instance and "createdAt" for dataset instances

  if (createdAt == 0)
    createdAt = orionldState.requestTime;

  if (creDateP != NULL)
    creDateP->value.f = createdAt;
  else
  {
    LM_W(("No '%s' found in attribute '%s'", fieldName, dbAttrP->name));

    KjNode* createdAtP = kjFloat(orionldState.kjsonP, fieldName, createdAt);
    kjChildAdd(dbAttrP, createdAtP);
  }
}
