/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include <bson/bson.h>                                           // bson_t, ...
#include <mongoc/mongoc.h>                                       // MongoDB C Client Driver

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // StMongoc
#include "orionld/mongoc/mongocWriteLog.h"                       // MONGOC_WLOG
#include "orionld/mongoc/mongocConnectionGet.h"                  // mongocConnectionGet
#include "orionld/mongoc/mongocKjTreeToBson.h"                   // mongocKjTreeToBson
#include "orionld/mongoc/mongocEntityFieldReplace.h"             // Own interface



// -----------------------------------------------------------------------------
//
// mongocEntityFieldReplace -
//
bool mongocEntityFieldReplace(const char* entityId, const char* dbFieldPath, KjNode* newValue, char** detailP)
{
  mongocConnectionGet(orionldState.tenantP, DbEntities);

  bson_t selector;
  bson_init(&selector);
  bson_append_utf8(&selector, "_id.id", 6, entityId, -1);

  bson_t set;
  bson_t request;
  bson_t reply;

  bson_init(&request);
  bson_init(&reply);
  bson_init(&set);

  bson_t dataset;
  mongocKjTreeToBson(newValue, &dataset);

  if (newValue->type == KjArray)
    bson_append_array(&set, dbFieldPath, -1, &dataset);
  else if (newValue->type == KjObject)
    bson_append_document(&set, dbFieldPath, -1, &dataset);
  else
    KT_X(1, "501 - the JSON type '%s' is not supported - easy fix - needs recompilation", kjValueType(newValue->type));

  bson_destroy(&dataset);

  bson_append_document(&request, "$set", 4, &set);
  bson_destroy(&set);

  MONGOC_WLOG("Replacing a field", orionldState.tenantP->mongoDbName, "entities", &selector, &request, StMongoc);
  bool dbResult = mongoc_collection_update_one(orionldState.mongoc.entitiesP, &selector, &request, NULL, &reply, &orionldState.mongoc.error);
  if (dbResult == false)
  {
    bson_error_t* errP = &orionldState.mongoc.error;
    *detailP = errP->message;
    KT_E("mongoc error replacing a filed of entity '%s': [%d.%d]: %s", entityId, errP->domain, errP->code, errP->message);
  }

  bson_destroy(&request);
  bson_destroy(&reply);

  return dbResult;
}
