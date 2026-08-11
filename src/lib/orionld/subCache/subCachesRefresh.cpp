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
#include <pthread.h>                                             // pthread_create, pthread_detach

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
}

#include "common/sem.h"                                          // cacheSemTake, cacheSemGive

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant, tenant0
#include "orionld/common/orionldState.h"                         // orionldState, subCacheInterval
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
// subCachesRefreshThread -
//
static void* subCachesRefreshThread(void* vP)
{
  //
  // A thread of its own - no request, so its own orionldState (and its own kalloc
  // buffer, which is reset after every tick).
  //
  orionldStateInit(NULL);

  while (1)
  {
    sleep(subCacheInterval);

    subCachesRefresh();

    kaBufferReset(&orionldState.kalloc, true);
    orionldStateRelease();
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// subCachesRefreshStart -
//
void subCachesRefreshStart(void)
{
  pthread_t  tid;
  int        ret;

  KT_T(KtSubCache, "Starting the subscription cache refresh thread (every %d seconds)", subCacheInterval);

  ret = pthread_create(&tid, NULL, subCachesRefreshThread, NULL);

  if (ret != 0)
    KT_RVE("Runtime Error (unable to create the subscription cache refresh thread: %d)", ret);

  pthread_detach(tid);
}
