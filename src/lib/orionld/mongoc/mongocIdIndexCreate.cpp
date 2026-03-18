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
#include <stdio.h>                                                // snprintf
#include <string.h>                                               // strncpy

extern "C"
{
#include "ktrace/kTrace.h"                                        // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                       // kaAlloc
#include "kalloc/kaStrdup.h"                                      // kaStrdup
}

#include "orionld/common/orionldState.h"                          // kalloc
#include "orionld/common/dotForEq.h"                              // dotForEq
#include "orionld/mongoc/mongocConnectionGet.h"                   // mongocConnectionGet
#include "orionld/mongoc/mongocIdIndexCreate.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// mongocIndexCreate - create an index on the entities collection
//
static bool mongocIndexCreate(mongoc_database_t* dbP, bson_t* keyP, const char* indexName)
{
  char*   collectionName    = (char*) "entities";
  bson_t* createIndexCommand = BCON_NEW("createIndexes",
                                        BCON_UTF8(collectionName),
                                        "indexes",
                                        "[",
                                        "{",
                                        "key",
                                        BCON_DOCUMENT(keyP),
                                        "name",
                                        BCON_UTF8(indexName),
                                        "}",
                                        "]");

  bson_error_t  mcError;
  bson_t        reply;
  bool          ok = true;

  if (mongoc_database_write_command_with_opts(dbP, createIndexCommand, NULL, &reply, &mcError) == false)
  {
    KT_E("Database Error (creating index '%s': %s)", indexName, mcError.message);
    ok = false;
  }

  bson_destroy(createIndexCommand);
  bson_destroy(&reply);

  return ok;
}



// -----------------------------------------------------------------------------
//
// mongocIdIndexCreate -
//
bool mongocIdIndexCreate(OrionldTenant* tenantP)
{
  mongocConnectionGet(NULL, DbNone);

  mongoc_database_t* dbP = mongoc_client_get_database(orionldState.mongoc.client, tenantP->mongoDbName);
  bool               ok  = true;
  bson_t             key;

  // Index on _id.id (for entity lookups by ID)
  bson_init(&key);
  BSON_APPEND_INT32(&key, "_id.id", 1);
  if (mongocIndexCreate(dbP, &key, "_id.id_1") == false)
    ok = false;
  bson_destroy(&key);

  // Compound index on _id.type + creDate + _id.id (for entity queries sorted by creation date)
  bson_init(&key);
  BSON_APPEND_INT32(&key, "_id.type", 1);
  BSON_APPEND_INT32(&key, "creDate",  1);
  BSON_APPEND_INT32(&key, "_id.id",   1);
  if (mongocIndexCreate(dbP, &key, "_id.type_1_creDate_1__id.id_1") == false)
    ok = false;
  bson_destroy(&key);

  mongoc_database_destroy(dbP);

  return ok;
}
