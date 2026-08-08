/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include <string.h>                                                 // strncpy

extern "C"
{
#include "ktrace/kTrace.h"                                          // KT_*
#include "kprom/kprom.h"                                            // kpromCounterInc
}

#include "orionld/types/SubCacheItem.h"                             // SubCacheItem
#include "orionld/common/orionldState.h"                            // promNotifications, promNotificationsFailed, cSubCounters
#include "orionld/common/traceLevels.h"                             // KTrace levels
#include "orionld/mongoc/mongocSubCountersUpdate.h"                 // mongocSubCountersUpdate
#include "orionld/subCache/subCacheItemCountersFlush.h"             // subCacheItemCountersFlush
#include "orionld/subCache/subCacheItemStatusSet.h"                 // subCacheItemStatusSet
#include "orionld/notifications/notificationFailure.h"              // Own interface



// -----------------------------------------------------------------------------
//
// notificationFailure -
//
void notificationFailure(SubCacheItem* subP, const char* errorReason, double notificationTime)
{
  KT_T(KtNotificationStats, "%s: notification failure (timestamp: %f)", subP->subId, notificationTime);
  bool forcedToPause = false;

  subP->lastNotificationTime  = notificationTime;
  subP->lastFailure           = notificationTime;
  subP->consecutiveErrors    += 1;
  subP->deltas.timesSent     += 1;
  subP->deltas.timesFailed   += 1;

  strncpy(subP->lastErrorReason, errorReason, sizeof(subP->lastErrorReason) - 1);

  // Force the subscription into "paused" due to too many consecutive errors
  if (subP->consecutiveErrors >= 3)
  {
    KT_T(KtNotificationStats, "%s: force the subscription into PAUSE due to 3 consecutive errors", subP->subId);
    subCacheItemStatusSet(subP, "paused");
    forcedToPause = true;
  }

  kpromCounterInc(promNotifications);
  kpromCounterInc(promNotificationsFailed);

  //
  // Flush to the database? Always if the subscription was just paused - that is
  // not a counter, it is a state change the next broker restart must see.
  //
  if (((cSubCounters != 0) && (subP->deltas.timesSent >= cSubCounters)) || (forcedToPause == true))
    subCacheItemCountersFlush(orionldState.tenantP, subP, forcedToPause);
  else
    KT_T(KtNotificationStats, "%s: no counter flush (cSubCounters: %d, timesSent: %d)", subP->subId, cSubCounters, subP->deltas.timesSent);
}



// -----------------------------------------------------------------------------
//
// notificationFailure -
//
void notificationFailure(PernotSubscription* pSubP, const char* errorReason, double notificationTime)
{
  KT_T(KtNotificationStats, "%s: notification failure (timestamp: %f)", pSubP->subscriptionId, notificationTime);
  bool forcedToPause = false;

  pSubP->lastNotificationTime   = notificationTime;
  pSubP->lastFailureTime        = notificationTime;
  pSubP->consecutiveErrors     += 1;
  pSubP->notificationAttempts  += 1;
  pSubP->dirty                 += 1;

  strncpy(pSubP->lastErrorReason, errorReason, sizeof(pSubP->lastErrorReason) - 1);

  // Force the subscription into "paused" due to too many consecutive errors
  if (pSubP->consecutiveErrors >= 3)
  {
    KT_T(KtNotificationStats, "%s: force the subscription into PAUSE due to 3 consecutive errors", pSubP->subscriptionId);
    pSubP->isActive = false;
    pSubP->state    = SubPaused;
    forcedToPause   = true;
  }

  kpromCounterInc(promNotifications);
  kpromCounterInc(promNotificationsFailed);

  KT_T(KtNotificationStats, "%s: dirty: %d, cSubCounters: %d", pSubP->subscriptionId, pSubP->dirty, cSubCounters);

  //
  // Flush to DB?
  // - If forcedToPause
  // - If pSubP->dirty (number of counter updates since last flush) >= cSubCounters
  //   - AND cSubCounters != 0
  //
  if (((cSubCounters != 0) && (pSubP->dirty >= cSubCounters)) || (forcedToPause == true))
  {
    KT_T(KtNotificationStats, "%s: Calling mongocSubCountersUpdate", pSubP->subscriptionId);

    // Save to database
    mongocSubCountersUpdate(pSubP->tenantP,
                            pSubP->subscriptionId,
                            true,
                            pSubP->notificationAttempts,
                            pSubP->notificationErrors,
                            pSubP->noMatch,
                            pSubP->lastNotificationTime,
                            pSubP->lastSuccessTime,
                            pSubP->lastFailureTime,
                            forcedToPause);

    // Reset counters
    pSubP->dirty                   = 0;
    pSubP->notificationAttemptsDb += pSubP->notificationAttempts;
    pSubP->notificationAttempts    = 0;
    pSubP->notificationErrorsDb   += pSubP->notificationErrors;
    pSubP->notificationErrors      = 0;
    pSubP->noMatchDb              += pSubP->noMatch;
    pSubP->noMatch                 = 0;
  }
  else
    KT_T(KtNotificationStats, "%s: Not calling mongocSubCountersUpdate (cSubCounters: %d, dirty: %d, forcedToPause: %s)",
         pSubP->subscriptionId,
         cSubCounters,
         pSubP->dirty,
         (forcedToPause == true)? "true" : "false");
}



// -----------------------------------------------------------------------------
//
// notificationFailure -
//
void notificationFailure(SubCacheItem* cSubP, PernotSubscription* pSubP, const char* errorReason, double notificationTime)
{
  if (cSubP != NULL)
    notificationFailure(cSubP, errorReason, notificationTime);
  else
    notificationFailure(pSubP, errorReason, notificationTime);
}
