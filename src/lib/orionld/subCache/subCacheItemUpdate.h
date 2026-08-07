#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMUPDATE_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMUPDATE_H_

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
extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem



// -----------------------------------------------------------------------------
//
// subCacheItemUpdate - replace a cached subscription's tree and recompile it
//
// IN PLACE: the item keeps its identity and its position in the cache, so pointers
// held elsewhere stay valid and the order of GET /subscriptions does not change.
//
// 'subP' is the new, complete API-model Subscription; it is cloned, so the caller
// keeps ownership. A NULL 'jsonldContextP' leaves the item's @context untouched.
//
extern void subCacheItemUpdate(SubCacheItem* sciP, KjNode* subP, OrionldContext* jsonldContextP);

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMUPDATE_H_
