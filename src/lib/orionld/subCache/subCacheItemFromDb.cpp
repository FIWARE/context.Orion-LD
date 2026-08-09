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
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/OrionldRenderFormat.h"                   // OrionldRenderFormat
#include "orionld/types/QNode.h"                                 // QNode
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/contextCache/orionldContextCacheLookup.h"      // orionldContextCacheLookup
#include "orionld/dbModel/dbModelToApiSubscription.h"            // dbModelToApiSubscription
#include "orionld/mongoc/mongocSubscriptionLookup.h"             // mongocSubscriptionLookup
#include "orionld/subCache/subCacheItemAdd.h"                    // subCacheItemAdd
#include "orionld/subCache/subCacheItemLookup.h"                 // subCacheItemLookup
#include "orionld/subCache/subCacheItemUpdate.h"                 // subCacheItemUpdate
#include "orionld/subCache/subCacheItemFromDb.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemFromDb - (re)build a cache item by reading the subscription back
//
// The NGSIv2 write paths build an ngsiv2::Subscription and a BSONObj, neither of
// which the new cache speaks - and translating a 25-field legacy insert by hand
// is exactly how the two copies drift apart. So instead the subscription is read
// back from the database and run through dbModelToApiSubscription, the SAME
// function the startup loader and the NGSI-LD PATCH path use. One shape, built by
// one piece of code, whichever API wrote it.
//
// Costs one extra read per NGSIv2 subscription create/update. Neither is a hot
// path, and correctness of the cache is worth more than the round trip.
//
bool subCacheItemFromDb(OrionldTenant* tenantP, const char* subscriptionId)
{
  if ((tenantP == NULL) || (tenantP->subCache == NULL))
    KT_RE(false, "No subscription cache for the tenant - '%s' will not be cached", subscriptionId);

  //
  // Filling a cache must never change the RESPONSE.
  //
  // Compiling a cached subscription runs the payload checks again (pcheckGeoQ,
  // qBuild, ...) and those report by calling orionldError, which sets the status
  // code of the request in flight. This is called after the subscription has been
  // created and the Location header added, so a check that is stricter than the
  // one the create path used would turn a 201 into a 400 - with the Location
  // header still in place, a response no code path is meant to produce.
  //
  // A subscription that is already in the database is not made invalid by this
  // broker failing to cache it: the cache warns, the API answer stands.
  //
  int                   savedStatusCode = orionldState.httpStatusCode;
  OrionldProblemDetails savedPd         = orionldState.pd;

  KjNode* dbSubP = mongocSubscriptionLookup(subscriptionId);

  if (dbSubP == NULL)
  {
    orionldState.httpStatusCode = savedStatusCode;
    orionldState.pd             = savedPd;
    KT_RE(false, "Subscription '%s' not found in the database - not cached", subscriptionId);
  }

  QNode*               qNodeP       = NULL;
  KjNode*              coordinatesP = NULL;
  KjNode*              contextNodeP = NULL;
  KjNode*              showChangesP = NULL;
  KjNode*              sysAttrsP    = NULL;
  OrionldRenderFormat  renderFormat = RF_NORMALIZED;
  double               timeInterval = 0;

  KjNode* apiSubP = dbModelToApiSubscription(dbSubP,
                                             tenantP->tenant,
                                             true,
                                             &qNodeP,
                                             &coordinatesP,
                                             &contextNodeP,
                                             &showChangesP,
                                             &sysAttrsP,
                                             &renderFormat,
                                             &timeInterval);
  if (apiSubP == NULL)
  {
    orionldState.httpStatusCode = savedStatusCode;
    orionldState.pd             = savedPd;
    KT_RE(false, "Subscription '%s': unable to convert it to API format - not cached", subscriptionId);
  }

  //
  // A Periodic Notification subscription is not driven by alterations and has a
  // cache of its own - it does not belong here.
  //
  if (timeInterval != 0)
  {
    orionldState.httpStatusCode = savedStatusCode;
    orionldState.pd             = savedPd;
    return true;
  }

  //
  // An NGSIv2 subscription has no @context. An NGSI-LD one does, and it is looked
  // up in the context CACHE - never downloaded.
  //
  // orionldContextFromUrl() would fetch it on a miss, and that has two costs
  // filling a cache must not pay: network I/O on a write path (see issue #1977,
  // where doing it at startup kept the broker from ever opening its port), and a
  // re-registration that overwrites how the context got here - an inline @context
  // served by this very broker came back marked "Downloaded" instead of
  // "FromInline".
  //
  // The @context was resolved when the subscription was created, so it is in the
  // cache. If it somehow is not, the subscription is cached without one rather
  // than the broker going to the network behind the caller's back.
  //
  OrionldContext* contextP           = NULL;
  KjNode*         jsonldContextNodeP = kjLookup(apiSubP, "jsonldContext");

  if (jsonldContextNodeP != NULL)
  {
    contextP = orionldContextCacheLookup(jsonldContextNodeP->value.s);

    if (contextP == NULL)
      KT_W("Subscription '%s': its @context '%s' is not in the context cache - cached without it",
           subscriptionId, jsonldContextNodeP->value.s);
  }

  SubCacheItem* sciP = subCacheItemLookup(tenantP->subCache, subscriptionId);

  if (sciP != NULL)
    subCacheItemUpdate(sciP, apiSubP, contextP);
  else
    subCacheItemAdd(tenantP->subCache, subscriptionId, apiSubP, true, contextP);

  KT_T(KtSubCache, "Subscription '%s' %s the sub cache from the database", subscriptionId, (sciP != NULL)? "updated in" : "added to");

  orionldState.httpStatusCode = savedStatusCode;
  orionldState.pd             = savedPd;

  return true;
}
