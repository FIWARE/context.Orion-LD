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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjInteger, kjFloat, kjChildAdd
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocSubCountersUpdate.h"              // mongocSubCountersUpdate
#include "orionld/subCache/subCacheItemCountersFlush.h"          // Own interface



// -----------------------------------------------------------------------------
//
// counterAdd - add a delta to a counter in the subscription tree
//
static void counterAdd(KjNode* notificationP, const char* name, uint32_t delta)
{
  if (delta == 0)
    return;

  KjNode* counterP = kjLookup(notificationP, name);

  if (counterP == NULL)
    kjChildAdd(notificationP, kjInteger(NULL, name, delta));
  else
    counterP->value.i += delta;
}



// -----------------------------------------------------------------------------
//
// timestampSet - set a timestamp in the subscription tree
//
static void timestampSet(KjNode* notificationP, const char* name, double timestamp)
{
  if (timestamp <= 0)
    return;

  KjNode* nodeP = kjLookup(notificationP, name);

  if (nodeP == NULL)
    kjChildAdd(notificationP, kjFloat(NULL, name, timestamp));
  else
  {
    nodeP->type    = KjFloat;
    nodeP->value.f = timestamp;
  }
}



// -----------------------------------------------------------------------------
//
// subCacheItemCountersFlush - push the notification counters to the database
//
// The counters inside the subscription tree are what the DATABASE holds, and
// 'deltas' is what has happened since. Flushing $inc's the deltas into the
// database, so they have to be added to the tree at the same time and zeroed -
// otherwise the next flush would count them twice, and a reader summing tree +
// deltas would see them twice until then.
//
void subCacheItemCountersFlush(OrionldTenant* tenantP, SubCacheItem* sciP, bool forcedToPause)
{
  KT_T(KtNotificationStats, "%s: flushing the notification counters (timesSent: %d, timesFailed: %d)",
       sciP->subId, sciP->deltas.timesSent, sciP->deltas.timesFailed);

  mongocSubCountersUpdate(tenantP,
                          sciP->subId,
                          sciP->ngsild,
                          sciP->deltas.timesSent,
                          sciP->deltas.timesFailed,
                          0,
                          sciP->lastNotificationTime,
                          sciP->lastSuccess,
                          sciP->lastFailure,
                          forcedToPause);

  KjNode* notificationP = kjLookup(sciP->subTree, "notification");

  if (notificationP != NULL)
  {
    counterAdd(notificationP,  "timesSent",        sciP->deltas.timesSent);
    counterAdd(notificationP,  "timesFailed",      sciP->deltas.timesFailed);
    timestampSet(notificationP, "lastNotification", sciP->lastNotificationTime);
    timestampSet(notificationP, "lastSuccess",      sciP->lastSuccess);
    timestampSet(notificationP, "lastFailure",      sciP->lastFailure);
  }

  sciP->deltas.timesSent   = 0;
  sciP->deltas.timesFailed = 0;
}
