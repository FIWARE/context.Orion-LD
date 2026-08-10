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
#include <string>                                                // std::string

extern "C"
{
#include "kbase/kTime.h"                                         // kTimeGet
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "common/sem.h"                                          // cacheSemTake, cacheSemGive
#include "alarmMgr/alarmMgr.h"                                   // alarmMgr

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/orionldTenantLookup.h"                  // orionldTenantLookup
#include "orionld/mongoc/mongocSubCountersUpdate.h"              // mongocSubCountersUpdate
#include "orionld/subCache/subCacheItemLookup.h"                 // subCacheItemLookup
#include "orionld/subCache/subCacheItemStatsUpdate.h"            // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemStatsUpdate - record the outcome of a notification attempt
//
// The NGSIv2 notification threads (senderThread, QueueWorkers) know the outcome of
// a notification but not the subscription it belongs to - only its tenant and id.
// So the item is looked up here, and the same three fields the NGSI-LD path sets
// (notificationSuccess/notificationFailure) are set.
//
// The counters are NOT touched: the sent-counter is stepped where the notification
// is dispatched (addTriggeredSubscriptions' caller), so counting here as well would
// count every notification twice.
//
void subCacheItemStatsUpdate(const char* tenantName, const char* subscriptionId, bool ngsild, bool failure)
{
  struct timespec ts;

  kTimeGet(&ts);
  double now = ts.tv_sec + ts.tv_nsec / 1000000000.0;

  OrionldTenant* tenantP = orionldTenantLookup(tenantName);

  if (tenantP == NULL)
  {
    KT_W("no such tenant '%s' (subId: '%s') - counters/timestamps lost", tenantName, subscriptionId);
    return;
  }

  //
  // Without a cache there is nowhere to accumulate - the database is written on
  // the spot, exactly as the old sub-cache did for -noCache.
  //
  if (noCache == true)
  {
    if (failure == false)
      mongocSubCountersUpdate(tenantP, subscriptionId, ngsild, 0, 0, 0, now, now, -1, false);
    else
      mongocSubCountersUpdate(tenantP, subscriptionId, ngsild, 0, 1, 0, now, -1, now, false);

    return;
  }

  if (tenantP->subCache == NULL)
  {
    KT_W("no sub cache for tenant '%s' (subId: '%s') - counters/timestamps lost", tenantName, subscriptionId);
    return;
  }

  cacheSemTake(__FUNCTION__, "Looking up an item for lastSuccess/Failure");

  SubCacheItem* sciP = subCacheItemLookup(tenantP->subCache, subscriptionId);

  if (sciP == NULL)
  {
    cacheSemGive(__FUNCTION__, "Looking up an item for lastSuccess/Failure");

    const char* errorString = "intent to update error status of non-existing subscription";

    alarmMgr.badInput(orionldState.clientIp, errorString);
    KT_W("no sub found (subId: '%s') - counters/timestamps lost", subscriptionId);
    return;
  }

  sciP->lastNotificationTime = now;

  if (failure == false)
  {
    sciP->lastSuccess = now;
    KT_T(KtSubCacheStats, "%s: Setting lastSuccess to %f (in cache)", sciP->subId, sciP->lastSuccess);
  }
  else
  {
    sciP->lastFailure = now;
    KT_T(KtSubCacheStats, "%s: Setting lastFailure to %f (in cache)", sciP->subId, sciP->lastFailure);
  }

  cacheSemGive(__FUNCTION__, "Looking up an item for lastSuccess/Failure");
}
