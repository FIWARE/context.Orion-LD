#ifndef SRC_LIB_ORIONLD_REGCACHE_REGCACHESEM_H_
#define SRC_LIB_ORIONLD_REGCACHE_REGCACHESEM_H_

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
#include "common/sem.h"                                          // SemOpType
#include "orionld/types/RegCache.h"                              // RegCache



// -----------------------------------------------------------------------------
//
// The registration cache lock
//
// One lock per registration cache, i.e. per tenant, living inside the RegCache itself.
// It protects the 'regList'/'last' linked list - NOT the contents of the items.
//
// ⚠️ NEVER hold two cache locks at the same time. A function that needs to loop over the
//    registration caches of several tenants takes and gives the lock once per tenant.
//
extern void regCacheSemInit(RegCache* rcP);
extern void regCacheSemTake(RegCache* rcP, const char* who, const char* what, SemOpType opType);
extern void regCacheSemGive(RegCache* rcP, const char* who, const char* what);



// -----------------------------------------------------------------------------
//
// regCacheItemPin / regCacheItemUnpin - keep an item alive past the walk that found it
//
// The lock above protects the LIST; it says nothing about how long an item lives. A DistOp keeps
// its RegCacheItem* for the whole of a forwarded request - far beyond the walk - so a DELETE of
// that registration in the meantime would free it under the sender's feet.
//
// ⚠️ regCacheItemPin MUST be called while the READ LOCK IS HELD. That is what makes the item alive
//    at that instant; without it, the item could be freed between the walk seeing it and the pin.
// ⚠️ regCacheItemUnpin takes the WRITE LOCK itself, so it must NOT be called with the lock held.
//    It frees the item if it is the last holder and the registration has been deleted meanwhile.
//
extern void regCacheItemPin(RegCacheItem* rciP);
extern void regCacheItemUnpin(RegCacheItem* rciP);

#endif  // SRC_LIB_ORIONLD_REGCACHE_REGCACHESEM_H_
