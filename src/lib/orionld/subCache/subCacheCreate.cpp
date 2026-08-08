/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
#include <stdlib.h>                                              // malloc
#include <string.h>                                              // strncmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjNavigate.h"                                    // kjNavigate
}

#include "orionld/types/QNode.h"                                 // QNode
#include "orionld/types/OrionldRenderFormat.h"                   // OrionldRenderFormat
#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/context/orionldContextFromUrl.h"               // orionldContextFromUrl
#include "orionld/mongoc/mongocSubscriptionsIter.h"              // mongocSubscriptionsIter
#include "orionld/dbModel/dbModelToApiSubscription.h"            // dbModelToApiSubscription
#include "orionld/ws/wsEndpointUri.h"                            // WS_ENDPOINT_URI_PREFIX
#include "orionld/subCache/subCacheItemAdd.h"                    // subCacheItemAdd
#include "orionld/subCache/subCacheCreate.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// subIterFunc -
//
int subIterFunc(SubCache* scP, KjNode* dbSubP)
{
  //
  // Convert DB Sub to API Sub.
  //
  // dbModelToApiSubscription hands back some of the pieces it had to look at
  // anyway - the geo coordinates, the render format, ... They are taken and
  // dropped: they all point into request-scoped memory, and subCacheItemCompile
  // builds the same state from the cache item's own clone of the tree instead.
  //
  QNode*               qNodeP       = NULL;
  KjNode*              coordinatesP = NULL;
  KjNode*              contextNodeP = NULL;
  KjNode*              showChangesP = NULL;
  KjNode*              sysAttrsP    = NULL;
  OrionldRenderFormat  renderFormat = RF_NORMALIZED;
  double               timeInterval = 0;

  KjNode* apiSubP = dbModelToApiSubscription(dbSubP,
                                             scP->tenantP->tenant,
                                             true,
                                             &qNodeP,
                                             &coordinatesP,
                                             &contextNodeP,
                                             &showChangesP,
                                             &sysAttrsP,
                                             &renderFormat,
                                             &timeInterval);
  if (apiSubP == NULL)
    KT_RE(-1, "dbModelToApiSubscription failed");

  KjNode* subIdNodeP = kjLookup(apiSubP, "id");

  //
  // A subscription created over a WebSocket cannot outlive the broker - its
  // endpoint URI names a connection (urn:ngsi-ld:ws:<fd>) and no connection
  // survives a restart. One found here is leftover from a crash; the legacy
  // loader deletes it from the database, but that runs AFTER this one, so it
  // would be cached (and answered by GET) in the meantime.
  //
  const char* uriPath[] = { "notification", "endpoint", "uri", NULL };
  KjNode*     uriNodeP  = kjNavigate(apiSubP, uriPath, NULL, NULL);

  if ((uriNodeP != NULL) && (uriNodeP->type == KjString) &&
      (strncmp(uriNodeP->value.s, WS_ENDPOINT_URI_PREFIX, WS_ENDPOINT_URI_PREFIX_LEN) == 0))
  {
    KT_W("Not caching stale WS subscription '%s' (no WS connection can have survived a restart)",
         (subIdNodeP != NULL)? subIdNodeP->value.s : "unknown");
    return 0;
  }

  //
  // A Periodic Notification subscription ('timeInterval') is not driven by
  // alterations and it has a cache of its own - it belongs there, and only there.
  // The API-request path already makes that distinction; so does the sub-cache
  // refresh (mongocSubCachePopulateByTenant) - this one has to as well, or a
  // restart would put every pernot subscription in BOTH caches.
  //
  if (timeInterval != 0)
    return 0;

  // If a jsonldContext is given for the subscription, make sure it's valid
  OrionldContext* jsonldContextP = NULL;
  KjNode*         jsonldContextNodeP = kjLookup(apiSubP, "jsonldContext");
  if (jsonldContextNodeP != NULL)
  {
    //
    // The @context was downloaded and persisted when the subscription was
    // created (a subscription whose context cannot be fetched is never
    // created), and the context cache is loaded from the database before this
    // runs - so this resolves from the cache and does not go to the network.
    //
    jsonldContextP = orionldContextFromUrl(jsonldContextNodeP->value.s, NULL);

    if (jsonldContextP == NULL)
    {
      KT_W("Unable to resolve a Subscription @context for a sub-cache item");
      return 0;
    }
  }

  char* subId = (subIdNodeP != NULL)? subIdNodeP->value.s : (char*) "no:sub:id";

  // Insert cacheSubP in tenantP->subCache - it brings the tree into the cache shape itself
  subCacheItemAdd(scP, subId, apiSubP, true, jsonldContextP);

  return 0;
}



// -----------------------------------------------------------------------------
//
// subCacheCreate -
//
SubCache* subCacheCreate(OrionldTenant* tenantP, bool scanSubs)
{
  SubCache* scP = (SubCache*) malloc(sizeof(SubCache));

  if (scP == NULL)
    KT_RE(NULL, "Out of memory (attempt to create a subscription cache)");

  scP->tenantP  = tenantP;
  scP->subList  = NULL;
  scP->last     = NULL;

  if (scanSubs)
  {
    if (mongocSubscriptionsIter(scP, subIterFunc) != 0)
      KT_E("mongocSubscriptionsIter failed");
  }

  return scP;
}
