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
#include <mongoc/mongoc.h>                                       // MongoDB C Client Driver

extern "C"
{
#include "kalloc/kaStrdup.h"                                     // ksStrdup
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/mongoc/mongocConnectionGet.h"                  // mongocConnectionGet
#include "orionld/mongoc/mongocEntityDelete.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// mongocEntityDelete -
//
bool mongocEntityDelete(const char* entityId, char** detailP)
{
  mongocConnectionGet();

  if (orionldState.mongoc.entitiesP == NULL)
    orionldState.mongoc.entitiesP = mongoc_client_get_collection(orionldState.mongoc.client, orionldState.tenantP->mongoDbName, "csubs");

  bson_t            selector;
  bson_error_t      error;

  //
  // Create the filter for the query
  //
  bson_init(&selector);
  bson_append_utf8(&selector, "_id.id", 6, entityId, -1);

  //
  // Run the query
  //
  if (mongoc_collection_remove(orionldState.mongoc.entitiesP, MONGOC_REMOVE_SINGLE_REMOVE, &selector, NULL, &error) == false)
  {
    LM_E(("Database Error (mongoc_collection_remove returned %d.%d:%s)", error.domain, error.code, error.message));
    orionldError(OrionldInternalError, "Database Error", error.message, 500);
    *detailP = kaStrdup(&orionldState.kalloc, error.message);
    return false;
  }

  bson_destroy(&selector);

  return true;
}
