/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include <unistd.h>                                              // NULL
#include <bson/bson.h>                                           // bson_t, ...
#include <mongoc/mongoc.h>                                       // mongoc_cursor_t, ...

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjChildAdd
#include "kjson/kjRender.h"                                      // TMP: kjFastRender
}

#include "orionld/common/orionldState.h"                         // orionldState, mongocContextsSem
#include "orionld/mongoc/mongocConnectionGet.h"                  // mongocConnectionGet
#include "orionld/mongoc/mongocKjTreeFromBson.h"                 // mongocKjTreeFromBson
#include "orionld/contextCache/orionldContextCache.h"            // Own interface



// -----------------------------------------------------------------------------
//
// mongocContextCacheGet -
//
// -----------------------------------------------------------------------------
//
// mongocContextCacheGetById - one @context, by its local id
//
// Twin of mongocContextCacheGet, which reads them all. The HA sync needs exactly
// one: the @context another instance just created.
//
KjNode* mongocContextCacheGetById(const char* contextId)
{
  mongoc_cursor_t*  cursor;
  bson_t            bsonContext;
  const bson_t*     bsonContextP = &bsonContext;
  bson_t*           query        = BCON_NEW("_id", BCON_UTF8(contextId));
  KjNode*           contextNodeP = NULL;

  mongocConnectionGet(NULL, DbContexts);

  sem_wait(&mongocContextsSem);

  cursor = mongoc_collection_find_with_opts(orionldState.mongoc.contextsP, query, NULL, NULL);

  if (mongoc_cursor_next(cursor, &bsonContextP) == true)
  {
    char*  title;
    char*  detail;

    contextNodeP = mongocKjTreeFromBson(bsonContextP, &title, &detail);

    if (contextNodeP == NULL)
      KT_E("Database Error parsing the retrieved @context (%s: %s)", title, detail);
  }

  sem_post(&mongocContextsSem);

  bson_destroy(query);
  mongoc_cursor_destroy(cursor);

  return contextNodeP;
}
