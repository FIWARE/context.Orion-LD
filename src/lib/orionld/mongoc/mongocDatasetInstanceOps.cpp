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
#include <stdio.h>                                               // snprintf
#include <string.h>                                              // strlen

#include <bson/bson.h>                                           // bson_t, ...

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjChildAdd, kjFloat
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // StMongoc
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/mongoc/mongocConnectionGet.h"                  // mongocConnectionGet
#include "orionld/mongoc/mongocKjTreeToBson.h"                   // mongocKjTreeToBson
#include "orionld/mongoc/mongocDatasetInstanceOps.h"             // Own interface



// -----------------------------------------------------------------------------
//
// attrEqPath - return a kaAlloc'd "@datasets.<attrEqName>" string.
//
static char* attrEqPath(const char* attrLongName)
{
  char* eqName  = kaStrdup(&orionldState.kalloc, attrLongName);
  dotForEq(eqName);

  int   pathLen = 10 /* "@datasets." */ + (int) strlen(eqName) + 1;
  char* path    = (char*) kaAlloc(&orionldState.kalloc, pathLen);
  snprintf(path, pathLen, "@datasets.%s", eqName);
  return path;
}



// -----------------------------------------------------------------------------
//
// mongocDatasetInstancePush -
//
bool mongocDatasetInstancePush
(
  const char* entityId,
  const char* attrLongName,
  KjNode*     instanceTree
)
{
  if ((instanceTree == NULL) || (instanceTree->type != KjObject))
  {
    KT_W("instanceTree is NULL or not an Object - aborting $push");
    return false;
  }

  kjChildAdd(instanceTree, kjFloat(orionldState.kjsonP, "createdAt",  orionldState.requestTime));
  kjChildAdd(instanceTree, kjFloat(orionldState.kjsonP, "modifiedAt", orionldState.requestTime));

  char* path = attrEqPath(attrLongName);

  mongocConnectionGet(orionldState.tenantP, DbEntities);

  bson_t selector;
  bson_init(&selector);
  bson_append_utf8(&selector, "_id.id", 6, entityId, -1);

  bson_t instanceBson;
  bson_t pushDoc;
  bson_t setDoc;
  bson_t update;

  mongocKjTreeToBson(instanceTree, &instanceBson);

  bson_init(&pushDoc);
  bson_append_document(&pushDoc, path, -1, &instanceBson);

  bson_init(&setDoc);
  bson_append_double(&setDoc, "modDate", 7, orionldState.requestTime);

  bson_init(&update);
  bson_append_document(&update, "$push", 5, &pushDoc);
  bson_append_document(&update, "$set",  4, &setDoc);

  bson_t reply;
  bson_init(&reply);

  KT_T(StMongoc, "Mongo $push %s for entity '%s'", path, entityId);
  bool ok = mongoc_collection_update_one(orionldState.mongoc.entitiesP, &selector, &update, NULL, &reply, &orionldState.mongoc.error);
  if (ok == false)
    KT_E("mongoc $push @datasets failed for '%s': [%d.%d]: %s",
         entityId, orionldState.mongoc.error.domain, orionldState.mongoc.error.code, orionldState.mongoc.error.message);

  bson_destroy(&selector);
  bson_destroy(&instanceBson);
  bson_destroy(&pushDoc);
  bson_destroy(&setDoc);
  bson_destroy(&update);
  bson_destroy(&reply);

  return ok;
}



// -----------------------------------------------------------------------------
//
// mongocDatasetSubAttrSet -
//
bool mongocDatasetSubAttrSet
(
  const char* entityId,
  const char* attrLongName,
  const char* datasetIdStr,
  const char* subAttrName,
  KjNode*     subAttrTree
)
{
  if ((subAttrTree == NULL) || (subAttrTree->type != KjObject))
  {
    KT_W("subAttrTree is NULL or not an Object - aborting $set");
    return false;
  }

  char* base = attrEqPath(attrLongName);

  // <base>.$[elem].<subAttrName>
  int   subPathLen = (int) strlen(base) + 9 /* ".$[elem]." */ + (int) strlen(subAttrName) + 1;
  char* subPath    = (char*) kaAlloc(&orionldState.kalloc, subPathLen);
  snprintf(subPath, subPathLen, "%s.$[elem].%s", base, subAttrName);

  // <base>.$[elem].modifiedAt
  int   modPathLen = (int) strlen(base) + 9 + 10 /* "modifiedAt" */ + 1;
  char* modPath    = (char*) kaAlloc(&orionldState.kalloc, modPathLen);
  snprintf(modPath, modPathLen, "%s.$[elem].modifiedAt", base);

  mongocConnectionGet(orionldState.tenantP, DbEntities);

  bson_t selector;
  bson_init(&selector);
  bson_append_utf8(&selector, "_id.id", 6, entityId, -1);

  bson_t subAttrBson;
  bson_t setDoc;
  bson_t update;

  mongocKjTreeToBson(subAttrTree, &subAttrBson);

  bson_init(&setDoc);
  bson_append_document(&setDoc, subPath, -1, &subAttrBson);
  bson_append_double(&setDoc, modPath, -1, orionldState.requestTime);
  bson_append_double(&setDoc, "modDate", 7, orionldState.requestTime);

  bson_init(&update);
  bson_append_document(&update, "$set", 4, &setDoc);

  // arrayFilters: [{ "elem.datasetId": "<datasetIdStr>" }]
  bson_t opts;
  bson_t filtersArr;
  bson_t filter0;

  bson_init(&opts);
  bson_append_array_begin(&opts, "arrayFilters", 12, &filtersArr);
  bson_append_document_begin(&filtersArr, "0", 1, &filter0);
  bson_append_utf8(&filter0, "elem.datasetId", 14, datasetIdStr, -1);
  bson_append_document_end(&filtersArr, &filter0);
  bson_append_array_end(&opts, &filtersArr);

  bson_t reply;
  bson_init(&reply);

  KT_T(StMongoc, "Mongo $set %s for entity '%s' datasetId '%s'", subPath, entityId, datasetIdStr);
  bool ok = mongoc_collection_update_one(orionldState.mongoc.entitiesP, &selector, &update, &opts, &reply, &orionldState.mongoc.error);
  if (ok == false)
    KT_E("mongoc $set @datasets sub-attr failed for '%s': [%d.%d]: %s",
         entityId, orionldState.mongoc.error.domain, orionldState.mongoc.error.code, orionldState.mongoc.error.message);

  bson_destroy(&selector);
  bson_destroy(&subAttrBson);
  bson_destroy(&setDoc);
  bson_destroy(&update);
  bson_destroy(&filter0);
  bson_destroy(&filtersArr);
  bson_destroy(&opts);
  bson_destroy(&reply);

  return ok;
}



// -----------------------------------------------------------------------------
//
// mongocDatasetInstancePull -
//
bool mongocDatasetInstancePull
(
  const char* entityId,
  const char* attrLongName,
  const char* datasetIdStr
)
{
  char* path = attrEqPath(attrLongName);

  mongocConnectionGet(orionldState.tenantP, DbEntities);

  bson_t selector;
  bson_init(&selector);
  bson_append_utf8(&selector, "_id.id", 6, entityId, -1);

  bson_t matcher;
  bson_t pullDoc;
  bson_t setDoc;
  bson_t update;

  bson_init(&matcher);
  bson_append_utf8(&matcher, "datasetId", 9, datasetIdStr, -1);

  bson_init(&pullDoc);
  bson_append_document(&pullDoc, path, -1, &matcher);

  bson_init(&setDoc);
  bson_append_double(&setDoc, "modDate", 7, orionldState.requestTime);

  bson_init(&update);
  bson_append_document(&update, "$pull", 5, &pullDoc);
  bson_append_document(&update, "$set",  4, &setDoc);

  bson_t reply;
  bson_init(&reply);

  KT_T(StMongoc, "Mongo $pull %s for entity '%s' datasetId '%s'", path, entityId, datasetIdStr);
  bool ok = mongoc_collection_update_one(orionldState.mongoc.entitiesP, &selector, &update, NULL, &reply, &orionldState.mongoc.error);
  if (ok == false)
    KT_E("mongoc $pull @datasets failed for '%s': [%d.%d]: %s",
         entityId, orionldState.mongoc.error.domain, orionldState.mongoc.error.code, orionldState.mongoc.error.message);

  bson_destroy(&selector);
  bson_destroy(&matcher);
  bson_destroy(&pullDoc);
  bson_destroy(&setDoc);
  bson_destroy(&update);
  bson_destroy(&reply);

  return ok;
}



// -----------------------------------------------------------------------------
//
// mongocDatasetAttrUnset - $unset the whole "@datasets.<attr>" entry
//
// $pull of the last instance leaves "@datasets.<attr>" as an empty array (which
// renders as "<attr>": []), so when removing an action-tied attribute on
// completion the entire entry is $unset instead.
//
bool mongocDatasetAttrUnset
(
  const char* entityId,
  const char* attrLongName
)
{
  char* path = attrEqPath(attrLongName);

  mongocConnectionGet(orionldState.tenantP, DbEntities);

  bson_t selector;
  bson_init(&selector);
  bson_append_utf8(&selector, "_id.id", 6, entityId, -1);

  bson_t unsetDoc;
  bson_t setDoc;
  bson_t update;

  bson_init(&unsetDoc);
  bson_append_utf8(&unsetDoc, path, -1, "", 0);

  bson_init(&setDoc);
  bson_append_double(&setDoc, "modDate", 7, orionldState.requestTime);

  bson_init(&update);
  bson_append_document(&update, "$unset", 6, &unsetDoc);
  bson_append_document(&update, "$set",   4, &setDoc);

  bson_t reply;
  bson_init(&reply);

  KT_T(StMongoc, "Mongo $unset %s for entity '%s'", path, entityId);
  bool ok = mongoc_collection_update_one(orionldState.mongoc.entitiesP, &selector, &update, NULL, &reply, &orionldState.mongoc.error);
  if (ok == false)
    KT_E("mongoc $unset @datasets failed for '%s': [%d.%d]: %s",
         entityId, orionldState.mongoc.error.domain, orionldState.mongoc.error.code, orionldState.mongoc.error.message);

  bson_destroy(&selector);
  bson_destroy(&unsetDoc);
  bson_destroy(&setDoc);
  bson_destroy(&update);
  bson_destroy(&reply);

  return ok;
}
