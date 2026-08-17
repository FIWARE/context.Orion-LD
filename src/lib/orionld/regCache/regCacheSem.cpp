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
#include <pthread.h>                                             // pthread_rwlock_*

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "common/sem.h"                                          // SemOpType
#include "orionld/types/RegCache.h"                              // RegCache
#include "orionld/types/RegCacheItem.h"                          // RegCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/regCache/regCacheItemFree.h"                   // regCacheItemFree
#include "orionld/regCache/regCacheSem.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// regCacheSemInit -
//
void regCacheSemInit(RegCache* rcP)
{
  if (pthread_rwlock_init(&rcP->rwlock, NULL) != 0)
    KT_X(1, "Error initializing the rwlock of the registration cache");
}



// -----------------------------------------------------------------------------
//
// regCacheSemTake -
//
// SemReadOp lets any number of readers in at once; anything else takes the lock for writing.
//
void regCacheSemTake(RegCache* rcP, const char* who, const char* what, SemOpType opType)
{
  if (rcP == NULL)  // No cache, nothing to protect - e.g. a tenant whose cache isn't created yet
    return;

  KT_T(KtRegCache, "%s: taking the reg-cache lock of tenant '%s' for %s (%s)",
       who, rcP->tenantP->mongoDbName, (opType == SemReadOp)? "reading" : "writing", what);

  if (opType == SemReadOp)
    pthread_rwlock_rdlock(&rcP->rwlock);
  else
    pthread_rwlock_wrlock(&rcP->rwlock);
}



// -----------------------------------------------------------------------------
//
// regCacheSemGive -
//
void regCacheSemGive(RegCache* rcP, const char* who, const char* what)
{
  if (rcP == NULL)
    return;

  KT_T(KtRegCache, "%s: giving back the reg-cache lock of tenant '%s' (%s)", who, rcP->tenantP->mongoDbName, what);
  pthread_rwlock_unlock(&rcP->rwlock);
}



// -----------------------------------------------------------------------------
//
// regCacheItemPin -
//
// Called with the READ lock held, so the item is alive right now. Several readers may pin the same
// item at the same time, which is why the increment is atomic - the read lock does not serialise
// them with each other. Decrements happen under the WRITE lock, so they cannot overlap with this.
//
void regCacheItemPin(RegCacheItem* rciP)
{
  if (rciP == NULL)
    return;

  __sync_add_and_fetch(&rciP->refs, 1);
  KT_T(KtRegCache, "Pinned reg-cache item '%s' (refs now %u)", (rciP->regId != NULL)? rciP->regId : "(no id)", rciP->refs);
}



// -----------------------------------------------------------------------------
//
// regCacheItemUnpin -
//
// Takes the write lock itself - never call it with the lock already held.
//
// If this was the last holder of an item whose registration was deleted while it was in use, the
// freeing that regCacheItemRemove had to skip happens here. The item is already out of the list at
// that point, so nobody can find it any more and no new pin can appear.
//
void regCacheItemUnpin(RegCacheItem* rciP)
{
  if (rciP == NULL)
    return;

  RegCache* rcP = rciP->owner;

  regCacheSemTake(rcP, __FUNCTION__, "Unpinning a registration cache item", SemWriteOp);

  uint32_t refs = __sync_sub_and_fetch(&rciP->refs, 1);
  bool     dead = (refs == 0) && (rciP->removed == true);

  KT_T(KtRegCache, "Unpinned reg-cache item '%s' (refs now %u, removed: %s)",
       (rciP->regId != NULL)? rciP->regId : "(no id)", refs, (rciP->removed == true)? "yes" : "no");

  regCacheSemGive(rcP, __FUNCTION__, "Unpinning a registration cache item");

  //
  // Freed AFTER the lock is given back: the item is unlinked, so it is ours alone, and freeing a
  // whole KjNode tree is not something to do with the cache locked.
  //
  if (dead == true)
    regCacheItemFree(rciP);
}
