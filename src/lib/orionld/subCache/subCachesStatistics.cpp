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
#include <stdio.h>                                               // snprintf
#include <string.h>                                              // strlen

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant, tenant0
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/tenantList.h"                           // tenantList
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCachesStatistics.h"                // Own interface



// -----------------------------------------------------------------------------
//
// subCachesInserts, subCachesRemoves, subCachesUpdates -
//
int subCachesInserts = 0;
int subCachesRemoves = 0;
int subCachesUpdates = 0;



// -----------------------------------------------------------------------------
//
// cacheItems - the number of subscriptions in one tenant's cache
//
static int cacheItems(OrionldTenant* tenantP)
{
  int items = 0;

  if (tenantP->subCache == NULL)
    return 0;

  for (SubCacheItem* sciP = tenantP->subCache->subList; sciP != NULL; sciP = sciP->next)
    ++items;

  return items;
}



// -----------------------------------------------------------------------------
//
// subCachesItems -
//
int subCachesItems(void)
{
  int items = cacheItems(&tenant0);

  for (OrionldTenant* tenantP = tenantList; tenantP != NULL; tenantP = tenantP->next)
    items += cacheItems(tenantP);

  return items;
}



// -----------------------------------------------------------------------------
//
// cacheIdListAppend - append one tenant's subscription ids to 'list'
//
// Returns false if they don't all fit - the caller then shows no list at all, as a
// truncated one would read as "these are the cached subscriptions" and be wrong.
//
static bool cacheIdListAppend(OrionldTenant* tenantP, char* list, int listSize)
{
  if (tenantP->subCache == NULL)
    return true;

  for (SubCacheItem* sciP = tenantP->subCache->subList; sciP != NULL; sciP = sciP->next)
  {
    int used      = strlen(list);
    int bytesLeft = listSize - used;

    if ((int) strlen(sciP->subId) + 2 >= bytesLeft)  // + 2: the ", " separator
      return false;

    if (used == 0)
      snprintf(list, listSize, "%s", sciP->subId);
    else
      snprintf(&list[used], bytesLeft, ", %s", sciP->subId);
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// subCachesStatisticsGet -
//
void subCachesStatisticsGet(int* inserts, int* removes, int* updates, int* items, char* list, int listSize)
{
  *inserts = subCachesInserts;
  *removes = subCachesRemoves;
  *updates = subCachesUpdates;
  *items   = subCachesItems();
  *list    = 0;

  if (listSize <= 128)
    return;

  bool complete = cacheIdListAppend(&tenant0, list, listSize);

  for (OrionldTenant* tenantP = tenantList; (complete == true) && (tenantP != NULL); tenantP = tenantP->next)
    complete = cacheIdListAppend(tenantP, list, listSize);

  if (complete == false)
    snprintf(list, listSize, "%s", "too many subscriptions to show the list");
}



// -----------------------------------------------------------------------------
//
// subCachesStatisticsReset -
//
void subCachesStatisticsReset(const char* by)
{
  KT_T(KtSubCache, "Resetting the subscription cache statistics (by %s)", by);

  subCachesInserts = 0;
  subCachesRemoves = 0;
  subCachesUpdates = 0;
}
