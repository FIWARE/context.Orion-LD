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
#include <unistd.h>                                              // sleep
#include <sys/time.h>                                            // gettimeofday
#include <errno.h>                                               // errno
#include <string.h>                                              // strerror
#include <pthread.h>                                             // pthread_create, pthread_detach

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
}

#include "common/sem.h"                                          // cacheSemTake, cacheSemGive

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant, tenant0
#include "orionld/common/orionldState.h"                         // orionldState, subCacheInterval, subCacheFlushInterval
#include "orionld/common/tenantList.h"                           // tenantList
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocSubCachePopulateByTenant.h"       // mongocSubCachePopulateByTenant
#include "orionld/subCache/subCachesCountersFlush.h"             // subCachesCountersFlush
#include "orionld/subCache/subCachesRefresh.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// subCachesRefresh -
//
void subCachesRefresh(void)
{
  //
  // The notification counters go to the database FIRST. What comes back from the
  // database is added to, not written over, but a flush here keeps the window in
  // which another instance sees a stale count as short as the refresh interval.
  //
  subCachesCountersFlush();

  cacheSemTake(__FUNCTION__, "Refreshing the subscription caches");

  mongocSubCachePopulateByTenant(&tenant0, true);

  for (OrionldTenant* tenantP = tenantList; tenantP != NULL; tenantP = tenantP->next)
    mongocSubCachePopulateByTenant(tenantP, true);

  cacheSemGive(__FUNCTION__, "Refreshing the subscription caches");
}



// -----------------------------------------------------------------------------
//
// now - seconds since the epoch, with a fraction
//
static double now(void)
{
  struct timeval tv;

  if (gettimeofday(&tv, NULL) != 0)
    KT_RE(0, "gettimeofday error: %s", strerror(errno));

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}



// -----------------------------------------------------------------------------
//
// subCachesMaintenanceThread - the counter flush, and (for now) the refresh
//
// Two independent deadlines, checked once a second:
//
//   -subCacheFlushIval:  push the notification counters to the database.
//   -subCacheIval:       poll the database for what other instances have done.
//
// The counters are ALSO flushed by the notification path itself, as soon as a
// subscription has -cSubCounters notifications pending (default 20). This timer
// is what gets the counters of a quiet subscription out - without it they sit in
// RAM until the twentieth notification, which for most subscriptions is never.
//
static void* subCachesMaintenanceThread(void* vP)
{
  double nextFlushAt   = now() + subCacheFlushInterval;
  double nextRefreshAt = now() + subCacheInterval;

  while (1)
  {
    sleep(1);

    double t = now();

    //
    // A thread of its own - no request, so its own orionldState, and its kalloc
    // buffer is reset once the tick is done with it.
    //
    if ((subCacheFlushInterval > 0) && (t >= nextFlushAt))
    {
      orionldStateInit(NULL);
      subCachesCountersFlush();
      kaBufferReset(&orionldState.kalloc, true);
      orionldStateRelease();

      nextFlushAt = now() + subCacheFlushInterval;
    }

    if ((subCacheInterval > 0) && (t >= nextRefreshAt))
    {
      orionldStateInit(NULL);
      subCachesRefresh();
      kaBufferReset(&orionldState.kalloc, true);
      orionldStateRelease();

      nextRefreshAt = now() + subCacheInterval;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// subCachesMaintenanceStart -
//
void subCachesMaintenanceStart(void)
{
  pthread_t  tid;
  int        ret;

  if ((subCacheFlushInterval <= 0) && (subCacheInterval <= 0))
  {
    KT_T(KtSubCache, "Neither -subCacheFlushIval nor -subCacheIval is set - no sub cache maintenance thread");
    return;
  }

  KT_T(KtSubCache, "Starting the sub cache maintenance thread (flush every %ds, refresh every %ds)",
       subCacheFlushInterval, subCacheInterval);

  ret = pthread_create(&tid, NULL, subCachesMaintenanceThread, NULL);

  if (ret != 0)
    KT_RVE("Runtime Error (unable to create the sub cache maintenance thread: %d)", ret);

  pthread_detach(tid);
}
