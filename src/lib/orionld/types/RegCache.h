#ifndef SRC_LIB_ORIONLD_TYPES_REGCACHE_H_
#define SRC_LIB_ORIONLD_TYPES_REGCACHE_H_

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
#include <pthread.h>                                             // pthread_rwlock_t

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/RegCacheItem.h"                          // RegCacheItem



// -----------------------------------------------------------------------------
//
// RegCache -
//
// The 'rwlock' protects 'regList' and 'last' - see regCacheSem.h.
// It lives HERE, in the cache it protects, and not in the tenant, so that each tenant's
// registration cache is locked independently of that tenant's OTHER caches.
// Readers take it for reading (many at a time), the mutators for writing.
//
typedef struct RegCache
{
  OrionldTenant*   tenantP;
  RegCacheItem*    regList;
  RegCacheItem*    last;
  pthread_rwlock_t rwlock;
} RegCache;

#endif  // SRC_LIB_ORIONLD_TYPES_REGCACHE_H_
