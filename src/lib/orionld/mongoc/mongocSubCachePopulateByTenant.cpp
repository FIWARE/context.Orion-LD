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
#include <string.h>                                              // strncmp
#include <mongoc/mongoc.h>                                       // MongoDB C Client Driver

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/QNode.h"                                 // QNode
#include "orionld/common/orionldState.h"                         // mongocPool
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/pernot/pernotSubCacheAdd.h"                    // pernotSubCacheAdd
#include "orionld/dbModel/dbModelToApiSubscription.h"            // dbModelToApiSubscription
#include "orionld/context/orionldContextFromUrl.h"               // orionldContextFromUrl
#include "orionld/kjTree/kjTreeLog.h"                            // KT_TREE
#include "orionld/mongoc/mongocWriteLog.h"                       // MONGOC_RLOG
#include "orionld/mongoc/mongocKjTreeFromBson.h"                 // mongocKjTreeFromBson
#include "orionld/subCache/subCacheItemAdd.h"                    // subCacheItemAdd    (the new sub cache)
#include "orionld/subCache/subCacheItemLookup.h"                 // subCacheItemLookup (the new sub cache)
#include "orionld/subCache/subCacheItemRemove.h"                 // subCacheItemRemove (the new sub cache)
#include "orionld/subCache/subCacheItemUpdate.h"                 // subCacheItemUpdate (the new sub cache)
#include "orionld/ws/wsEndpointUri.h"                            // WS_ENDPOINT_URI_PREFIX
#include "orionld/mongoc/mongocSubCachePopulateByTenant.h"       // Own interface



/* ****************************************************************************
*
* mongocSubCachePopulateByTenant -
*
* 2. Lookup all subscriptions in the database
* 3. Insert them again in the cache (with fresh data from database)
*
* NOTE
*   The query for the database ONLY extracts the interesting subscriptions:
*   - "conditions.type" << "ONCHANGE"
*
*   I.e. the subscriptions is for ONCHANGE.
*
* IMPORTANT NOTE:
*   As this function is called outside of the "Request Threads", orionldState cannot be used!
*/
bool mongocSubCachePopulateByTenant(OrionldTenant* tenantP, bool refresh)
{
  bson_t            mongoFilter;
  const bson_t*     mongoDocP;
  mongoc_cursor_t*  mongoCursorP;
  bson_error_t      mongoError;
  char*             title;
  char*             details;

  //
  // Empty filter for the query - we want ALL subscriptions
  //
  bson_init(&mongoFilter);

  mongoc_client_t*     connectionP    = mongoc_client_pool_pop(mongocPool);
  mongoc_collection_t* subscriptionsP = mongoc_client_get_collection(connectionP, tenantP->mongoDbName, "csubs");

  if (subscriptionsP == NULL)
    KT_X(1, "mongoc_client_get_collection failed for 'csubs' collection on tenant '%s'", tenantP->mongoDbName);

  //
  // Run the query
  //
  // semTake(&mongoSubscriptionsSem);
  MONGOC_RLOG("Subscription for sub-cache", tenantP->mongoDbName, "subscriptions", &mongoFilter, NULL, StMongoc);
  if ((mongoCursorP = mongoc_collection_find_with_opts(subscriptionsP, &mongoFilter, NULL, NULL)) == NULL)
  {
    mongoc_client_pool_push(mongocPool, connectionP);
    mongoc_collection_destroy(subscriptionsP);
    KT_RE(false, "Internal Error (mongoc_collection_find_with_opts ERROR)");
  }

  if (refresh == true)
  {
    //
    // Algorithm for "Delete subs that are no longer in DB":
    //        1. Before the loop: Mark all subs in cache as "not in DB"
    //        2. Inside the loop: When a sub is found in DB, mark it as "in DB"
    //        3. After the loop:  Lookup all cached subs and remove thos "not in DB"
    //

    // Mark every cached subscription of the tenant as "not in DB"
    if (tenantP->subCache != NULL)
    {
      for (SubCacheItem* sciP = tenantP->subCache->subList; sciP != NULL; sciP = sciP->next)
        sciP->inDB = false;
    }
  }

  while (mongoc_cursor_next(mongoCursorP, &mongoDocP))
  {
    KjNode* dbSubP = mongocKjTreeFromBson(mongoDocP, &title, &details);

    if (dbSubP == NULL)
    {
      KT_E("Database Error (unable to create tree of subscriptions for tenant '%s')", tenantP->tenant);
      continue;
    }

    //
    // Stale WS subscriptions: on startup, no WS connections exist, so any subscription
    // whose endpoint URI names one (urn:ngsi-ld:ws:<fd>) is leftover from a previous
    // crash.  Delete it from DB and skip.
    //
    // ONLY on startup. This same function is the -subCacheIval refresh, and there a
    // WS subscription naming a connection is the normal, healthy case - deleting it
    // would take down every live WebSocket subscription within one refresh tick.
    //
    KjNode* referenceP = (refresh == false)? kjLookup(dbSubP, "reference") : NULL;
    if ((referenceP != NULL) && (referenceP->type == KjString) && (strncmp(referenceP->value.s, WS_ENDPOINT_URI_PREFIX, WS_ENDPOINT_URI_PREFIX_LEN) == 0))
    {
      KjNode* subIdP = kjLookup(dbSubP, "_id");
      const char* subId = (subIdP != NULL) ? subIdP->value.s : "unknown";

      KT_W("Removing stale WS subscription '%s' (no WS connection can have survived a restart)", subId);

      // Delete from DB using the collection we already have open
      bson_t selector;
      bson_init(&selector);
      bson_append_utf8(&selector, "_id", 3, subId, -1);
      mongoc_collection_delete_one(subscriptionsP, &selector, NULL, NULL, NULL);
      bson_destroy(&selector);
      continue;
    }

    QNode*              qTree         = NULL;
    KjNode*             contextNodeP  = NULL;
    KjNode*             coordinatesP  = NULL;
    KjNode*             showChangesP  = NULL;
    KjNode*             sysAttrsP     = NULL;
    OrionldRenderFormat renderFormat  = RF_NORMALIZED;
    double              timeInterval  = 0;

    KT_TREE(dbSubP, "dbSubP", KtPernot);
    KjNode*      apiSubP       = dbModelToApiSubscription(dbSubP,
                                                          tenantP->tenant,
                                                          true,
                                                          &qTree,
                                                          &coordinatesP,
                                                          &contextNodeP,
                                                          &showChangesP,
                                                          &sysAttrsP,
                                                          &renderFormat,
                                                          &timeInterval);

    if (apiSubP == NULL)
      continue;

    KT_TREE(apiSubP, "apiSubP", KtPernot);

    OrionldContext* contextP = NULL;
    if (contextNodeP != NULL)
      contextP = orionldContextFromUrl(contextNodeP->value.s, NULL);

    if (timeInterval == 0)
    {
      //
      // TRANSITIONAL: this is the OLD sync mechanism (the -subCacheIval refresh),
      // on its way out - mongo change streams replace it. Until then it is what a
      // second broker instance learns from.
      //
      KjNode*       subIdNodeP = kjLookup(apiSubP, "id");
      const char*   subId      = (subIdNodeP != NULL)? subIdNodeP->value.s : NULL;
      SubCacheItem* sciP       = (subId != NULL)? subCacheItemLookup(tenantP->subCache, subId) : NULL;

      if (subId != NULL)
      {
        if (sciP == NULL)
          sciP = subCacheItemAdd(tenantP->subCache, subId, apiSubP, true, contextP);
        else
        {
          //
          // Unconditionally - NOT "only if 'modifiedAt' is newer".
          //
          // This refresh is how a broker learns what ANOTHER broker did, and it is
          // also how a subscription edited straight in the database (which is a
          // thing people do, and a thing the test suite does) reaches the cache.
          // Neither of those necessarily moves 'modifiedAt', so comparing it means
          // quietly serving a stale subscription forever.
          //
          // It does mean recompiling regexes, QNode trees and GEOS geometries once
          // per tick per subscription. That is the price of this mechanism, and it
          // is one more reason it is on its way out.
          //
          subCacheItemUpdate(sciP, apiSubP, contextP);
        }

        if (sciP != NULL)
          sciP->inDB = true;
      }

    }
    else
      pernotSubCacheAdd(NULL, apiSubP, NULL, qTree, coordinatesP, contextP, tenantP, showChangesP, sysAttrsP, renderFormat, timeInterval);
  }

  if (refresh == true)
  {
    //
    // Whatever was not found in the database has been deleted - by another broker
    // instance, or straight in mongo. Each cache is swept with its OWN mark.
    //
    if (tenantP->subCache != NULL)
    {
      SubCacheItem* sciP = tenantP->subCache->subList;

      while (sciP != NULL)
      {
        SubCacheItem* next = sciP->next;

        if ((sciP->inDB == false) && (sciP->cacheOnly == false))
          subCacheItemRemove(tenantP->subCache, sciP->subId);

        sciP = next;
      }
    }
  }

  mongoc_client_pool_push(mongocPool, connectionP);
  mongoc_collection_destroy(subscriptionsP);

  bool r = true;

  if (mongoc_cursor_error(mongoCursorP, &mongoError))
  {
    r = false;
    KT_E("Internal Error (DB Error '%s')", mongoError.message);
  }

  mongoc_cursor_destroy(mongoCursorP);
  bson_destroy(&mongoFilter);

  return r;
}
