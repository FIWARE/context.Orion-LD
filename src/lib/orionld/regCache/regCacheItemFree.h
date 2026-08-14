#ifndef SRC_LIB_ORIONLD_REGCACHE_REGCACHEITEMFREE_H_
#define SRC_LIB_ORIONLD_REGCACHE_REGCACHEITEMFREE_H_

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
#include "orionld/types/RegCacheItem.h"                          // RegCacheItem



// -----------------------------------------------------------------------------
//
// regCacheItemFree - free an item that is already unlinked from its cache
//
// ⚠️ The item MUST already be out of the list and MUST have no holders left (refs == 0).
//    Two callers: regCacheItemRemove, when the item it just unlinked was not pinned, and
//    regCacheItemUnpin, for the last holder of an item that was removed while in use.
//
extern void regCacheItemFree(RegCacheItem* rciP);

#endif  // SRC_LIB_ORIONLD_REGCACHE_REGCACHEITEMFREE_H_
