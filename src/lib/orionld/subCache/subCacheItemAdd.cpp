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
#include <stdlib.h>                                              // calloc, free
#include <string.h>                                              // strdup

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjChildAdd, kjInteger, kjFloat, kjString
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/apiModelToCacheSubscription.h"        // apiModelToCacheSubscription
#include "orionld/subCache/subCacheItemCompile.h"                // subCacheItemCompile
#include "orionld/subCache/subCachesStatistics.h"          // subCachesInserts
#include "orionld/subCache/subCacheItemAdd.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// subCounterAdd - seed a counter in the subscription tree, if not already there
//
static void subCounterAdd(KjNode* subP, const char* name)
{
  if (kjLookup(subP, name) == NULL)
    kjChildAdd(subP, kjInteger(NULL, name, 0));
}



// -----------------------------------------------------------------------------
//
// subTimestampAdd - seed a timestamp in the subscription tree, if not already there
//
static void subTimestampAdd(KjNode* subP, const char* name)
{
  if (kjLookup(subP, name) == NULL)
    kjChildAdd(subP, kjFloat(NULL, name, 0));
}



// -----------------------------------------------------------------------------
//
// subTimestampGet - a timestamp in the subscription tree is a Float, or absent
//
static double subTimestampGet(KjNode* containerP, const char* name)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if (nodeP == NULL)
    return 0;

  return (nodeP->type == KjFloat)? nodeP->value.f : nodeP->value.i;
}



// -----------------------------------------------------------------------------
//
// subStringAdd - seed a string in the subscription tree, if not already there
//
static void subStringAdd(KjNode* subP, const char* name, const char* value)
{
  if (kjLookup(subP, name) == NULL)
    kjChildAdd(subP, kjString(NULL, name, value));
}



// -----------------------------------------------------------------------------
//
// subCacheItemAdd -
//
SubCacheItem* subCacheItemAdd
(
  SubCache*        scP,
  const char*      subscriptionId,
  KjNode*          subP,
  bool             fromDb,
  OrionldContext*  jsonldContextP
)
{
  if (scP == NULL)
    KT_RE(NULL, "No subscription cache for the tenant - the sub '%s' will not be cached", subscriptionId);

  SubCacheItem* sciP = (SubCacheItem*) calloc(1, sizeof(SubCacheItem));

  if (sciP == NULL)
    KT_X(1, "Out of memory attempting to allocate a Subscription Cache Item (%d bytes)", sizeof(SubCacheItem));

  KT_T(KtSubCache, "Adding sub '%s' into the sub cache for tenant '%s'", subscriptionId, scP->tenantP->mongoDbName);

  //
  // Append - the cache keeps insertion order, exactly as the reg cache does.
  //
  if (scP->last == NULL)
    scP->subList = sciP;
  else
    scP->last->next = sciP;
  scP->last = sciP;

  sciP->subId    = strdup(subscriptionId);
  sciP->subTree  = kjClone(NULL, subP);
  sciP->contextP = jsonldContextP;
  sciP->dirty    = false;
  sciP->next     = NULL;

  //
  // Three write paths feed the cache and they do not agree on the details - the
  // clone is brought into the ONE shape the cache holds before anything reads it.
  //
  apiModelToCacheSubscription(sciP->subTree, jsonldContextP);

  //
  // An NGSI-LD Subscription id is a URI (and thus has a colon) and is stored as
  // the database _id verbatim; an NGSIv2 subscription has a mongo OID for _id,
  // rendered as a 24-character hex string. Whoever writes to the database needs
  // to know which of the two it is.
  //
  sciP->ngsild = (strchr(subscriptionId, ':') != NULL);

  ++subCachesInserts;

  //
  // The deltas are what keeps mongo out of the notification path: every
  // notification bumps them in RAM and they are flushed (added, not written)
  // now and then. They always start at zero for a freshly cached item - what
  // is already in the database is the database's business.
  //
  sciP->deltas.timesSent   = 0;
  sciP->deltas.timesFailed = 0;

  KjNode* hostAliasP = kjLookup(sciP->subTree, "hostAlias");
  if (hostAliasP != NULL)
    sciP->hostAlias = strdup(hostAliasP->value.s);

  //
  // A subscription that arrives from an API request has none of the bookkeeping
  // members yet - one coming from the database has them already.
  // The counters go where dbModelToApiSubscription puts them - inside
  // "notification" - while "status" is a top level member of a Subscription.
  //
  KjNode* notificationP = kjLookup(sciP->subTree, "notification");

  if (fromDb == false)
  {
    if (notificationP != NULL)
    {
      subCounterAdd(notificationP,   "timesSent");
      subCounterAdd(notificationP,   "timesFailed");
      subTimestampAdd(notificationP, "lastSuccess");
      subTimestampAdd(notificationP, "lastFailure");
    }

    //
    // 'status' is the broker's own view of the subscription and the API has only
    // three words for it (TS 104-175 clause 5.2.6.5.2): active, paused, expired.
    // A subscription created with "isActive": false starts out paused - the two
    // must never disagree, see subCacheItemStatusSet.
    //
    KjNode* isActiveP = kjLookup(sciP->subTree, "isActive");
    bool    active    = (isActiveP != NULL)? isActiveP->value.b : true;

    subStringAdd(sciP->subTree, "status", (active == true)? "active" : "paused");
  }

  //
  // The notification timestamps are lifted out of the tree - 'lastNotification' is
  // read on every single match (throttling), and all three are written on every
  // notification. They come from the database: a broker that restarts must not
  // notify a throttled subscription before its throttling has passed, nor forget
  // when the subscription last succeeded.
  //
  if (notificationP != NULL)
  {
    sciP->lastNotificationTime = subTimestampGet(notificationP, "lastNotification");
    sciP->lastSuccess          = subTimestampGet(notificationP, "lastSuccess");
    sciP->lastFailure          = subTimestampGet(notificationP, "lastFailure");
  }

  KjNode* modifiedAtP = kjLookup(sciP->subTree, "modifiedAt");
  if (modifiedAtP != NULL)
    sciP->modifiedAt = (modifiedAtP->type == KjFloat)? modifiedAtP->value.f : modifiedAtP->value.i;

  //
  // The compiled matching state comes LAST, and it is built from sciP->subTree -
  // the item's own clone. The caller's tree is request-scoped, so regexes, QNode
  // trees and GEOS geometries built from it would dangle after the request.
  //
  subCacheItemCompile(sciP);

  return sciP;
}
