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

#endif  // SRC_LIB_ORIONLD_REGCACHE_REGCACHESEM_H_
