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
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant, tenant0
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/tenantList.h"                           // tenantList
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheItemCountersFlush.h"          // subCacheItemCountersFlush
#include "orionld/subCache/subCachesCountersFlush.h"             // Own interface



// -----------------------------------------------------------------------------
//
// cacheFlush - flush every item of one tenant's cache that has pending counters
//
static void cacheFlush(OrionldTenant* tenantP)
{
  SubCache* scP = tenantP->subCache;

  if (scP == NULL)
    return;

  for (SubCacheItem* sciP = scP->subList; sciP != NULL; sciP = sciP->next)
  {
    if (sciP->deltas.timesSent > 0)
      subCacheItemCountersFlush(tenantP, sciP, false);
  }
}



// -----------------------------------------------------------------------------
//
// subCachesCountersFlush - flush the notification counters of every tenant
//
// Called from the sub-cache refresher, which is about to reload every cached
// subscription from the database. Anything not yet flushed has to reach the
// database FIRST, or the reload would overwrite it with the older value.
//
void subCachesCountersFlush(void)
{
  KT_T(KtSubCache, "Flushing the notification counters of all subscription caches");

  cacheFlush(&tenant0);

  for (OrionldTenant* tenantP = tenantList; tenantP != NULL; tenantP = tenantP->next)
    cacheFlush(tenantP);
}
