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
#include <mongoc/mongoc.h>                                       // MongoDB C Client Driver

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/common/orionldState.h"                         // orionldState, mongocPool
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocWriteLog.h"                       // MONGOC_RLOG
#include "orionld/mongoc/mongocKjTreeFromBson.h"                 // mongocKjTreeFromBson
#include "orionld/mongoc/mongocSubscriptionsIter.h"              // Own interface



// -----------------------------------------------------------------------------
//
// mongocSubscriptionsIter -
//
// Twin of mongocRegistrationsIter, for the "csubs" collection.
//
int mongocSubscriptionsIter(SubCache* scP, SubCacheIterFunc callback)
{
  bson_t                mongoFilter;
  const bson_t*         mongoDocP = NULL;
  mongoc_cursor_t*      mongoCursorP;
  bson_error_t          mongoError;
  mongoc_read_prefs_t*  readPrefs = mongoc_read_prefs_new(MONGOC_READ_NEAREST);
  char*                 title;
  char*                 details;
  int                   retVal    = 0;

  bson_init(&mongoFilter);

  //
  // A client popped straight from the pool, NOT mongocConnectionGet().
  //
  // mongocConnectionGet() fills in the request-scoped orionldState.mongoc.*
  // collection handles, and mongocConnectionRelease() pushes the client back
  // while leaving those handles pointing at it. That is fine inside a request,
  // where orionldState is per-request and torn down with it - but this runs at
  // STARTUP, on the main thread, and it leaked file descriptors (caught by
  // ngsild_issue_1441, the fd-leak test).
  //
  // This is the pattern the legacy mongocSubCachePopulateByTenant uses for the
  // very same collection, and for the same reason.
  //
  mongoc_client_t*     connectionP     = mongoc_client_pool_pop(mongocPool);
  mongoc_collection_t* subsCollectionP = mongoc_client_get_collection(connectionP, scP->tenantP->mongoDbName, "csubs");
  if (subsCollectionP == NULL)
    KT_X(1, "mongoc_client_get_collection failed for 'csubs' collection on tenant '%s'", scP->tenantP->mongoDbName);

  MONGOC_RLOG("Query for all subs", scP->tenantP->mongoDbName, "csubs", NULL, NULL, StMongoc);
  mongoCursorP = mongoc_collection_find_with_opts(subsCollectionP, &mongoFilter, NULL, readPrefs);
  if (mongoCursorP == NULL)
  {
    orionldError(OrionldInternalError, "Database Error", "mongoc_collection_find_with_opts ERROR", 500);
    mongoc_collection_destroy(subsCollectionP);
    mongoc_client_pool_push(mongocPool, connectionP);
    mongoc_read_prefs_destroy(readPrefs);
    bson_destroy(&mongoFilter);
    return 1;
  }

  int hits = 0;
  while (mongoc_cursor_next(mongoCursorP, &mongoDocP))
  {
    char* json = bson_as_relaxed_extended_json(mongoDocP, NULL);
    KT_T(StMongoc, "Found a subscription in the DB: '%s'", json);
    bson_free(json);

    KjNode* dbSubP = mongocKjTreeFromBson(mongoDocP, &title, &details);
    if (dbSubP == NULL)
    {
      orionldError(OrionldInternalError, "Database Error", "unable to convert DB-Model subscription to API Format", 500);
      continue;
    }

    if (callback(scP, dbSubP) != 0)
    {
      retVal = 2;
      break;
    }

    ++hits;
  }
  KT_T(StMongoc, "Found %d hits in the db", hits);

  if (mongoc_cursor_error(mongoCursorP, &mongoError))
  {
    retVal = 3;
    orionldError(OrionldInternalError, "Database Error", mongoError.message, 500);
  }

  mongoc_cursor_destroy(mongoCursorP);
  mongoc_collection_destroy(subsCollectionP);
  mongoc_client_pool_push(mongocPool, connectionP);
  mongoc_read_prefs_destroy(readPrefs);
  bson_destroy(&mongoFilter);

  return retVal;
}
