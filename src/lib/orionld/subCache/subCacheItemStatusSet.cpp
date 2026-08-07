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
#include "kjson/kjBuilder.h"                                     // kjString, kjChildAdd
}

#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/subCache/subCacheItemStatusSet.h"              // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemStatusSet - set the status of a cached subscription
//
// "status" is read-only for the Context Subscriber - it is the broker that
// decides when a subscription is "expired" or "paused". Both the tree (which is
// the source of truth) and the compiled 'isActive' are set, and they must never
// disagree - a paused subscription neither matches nor notifies.
//
void subCacheItemStatusSet(SubCacheItem* sciP, const char* status)
{
  bool    active    = (strcmp(status, "active") == 0);
  KjNode* statusP   = kjLookup(sciP->subTree, "status");
  KjNode* isActiveP = kjLookup(sciP->subTree, "isActive");

  if (statusP != NULL)
    statusP->value.s = (char*) status;  // kjFree doesn't free a KjString's value - a literal is safe here
  else
    kjChildAdd(sciP->subTree, kjString(NULL, "status", status));

  if (isActiveP != NULL)
    isActiveP->value.b = active;

  sciP->isActive = active;
}
