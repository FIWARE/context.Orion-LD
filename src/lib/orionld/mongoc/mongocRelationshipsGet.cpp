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
* Author: Carsten Frey
*/
#include <bson/bson.h>                                             // bson_t, ...
#include <mongoc/mongoc.h>                                         // MongoDB C Client Driver


extern "C"
{
#include "kalloc/kaStrdup.h"                                       // kaStrdup
#include "kjson/KjNode.h"                                          // KjNode
#include "kjson/kjBuilder.h"                                       // kjArray
#include "ktrace/kTrace.h"                                         // trace messages -
#include "kjson/kjLookup.h"                                        // kjLookup
}

#include "orionld/common/orionldState.h"                           // orionldState
#include "orionld/common/eqForDot.h"                               // eqForDot
#include "orionld/context/orionldContextItemAliasLookup.h"         // orionldContextItemAliasLookup
#include "orionld/mongoc/mongocConnectionGet.h"                    // mongocConnectionGet
#include "orionld/mongoc/mongocKjTreeFromBson.h"                   // mongocKjTreeFromBson
#include "orionld/mongoc/mongocRelationshipsGet.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// relExtractFromMongo -
//
static void relExtractFromMongo(KjNode* inputArray, KjNode* relArray)
{
  for (KjNode* arrItemP = inputArray->value.firstChildP; arrItemP != NULL; arrItemP = arrItemP->next)
  {
    KjNode* tNode    = kjLookup(arrItemP, "entityId");
    KjNode* attrName = kjLookup(arrItemP, "attrName");

    if (tNode == NULL)
    {
      KT_W("No entityId found in tree ...");
      continue;
    }

    if (attrName == NULL)
    {
      KT_W("No entityId found in tree ...");
      continue;
    }

    // Lookup alias for type name in context
    tNode->value.s = orionldContextItemAliasLookup(orionldState.contextP, tNode->value.s, NULL, NULL);

    char* attrEqName =  kaStrdup(&orionldState.kalloc, attrName->value.s);
    eqForDot(attrEqName);
    attrName->value.s = orionldContextItemAliasLookup(orionldState.contextP, attrEqName, NULL, NULL);

    // create new node with entityId and attribute name
    KjNode* relNode = kjString(orionldState.kjsonP, tNode->value.s, attrName->value.s);

    kjChildAdd(relArray, relNode);
  }
}



// -----------------------------------------------------------------------------
//
// mongocRelationshipsGet -
//
// PARAMETERS
//   entityName
//
// NOTE
// The local variable pipeline_json is a Pipeline-Array in JSON-Format with entityName parameter ->
// returns all entities having a relationship attribute pointing to entityName
//
KjNode* mongocRelationshipsGet(const char* entityName)
{
  //
  // We use a projection for getting all relationships of the given entity Name
  //
  bson_t*       pipeline = bson_new();
  bson_error_t  error;
  char          pipeline_json[1024];

  snprintf(pipeline_json, sizeof(pipeline_json),
    "["
    "  {"
    "    \"$addFields\": {"
    "      \"attrsArray\": { \"$objectToArray\": \"$attrs\" }"
    "    }"
    "  },"
    "  {"
    "    \"$addFields\": {"
    "      \"relAttr\": {"
    "        \"$first\": {"
    "          \"$filter\": {"
    "            \"input\": \"$attrsArray\","
    "            \"as\": \"a\","
    "            \"cond\": {"
    "              \"$and\": ["
    "                { \"$eq\": [\"$$a.v.type\", \"Relationship\"] },"
    "                { \"$eq\": [\"$$a.v.value\", \"%s\"] }"
    "              ]"
    "            }"
    "          }"
    "        }"
    "      }"
    "    }"
    "  },"
    "  {"
    "    \"$match\": {"
    "      \"relAttr\": { \"$ne\": null }"
    "    }"
    "  },"
    "  {"
    "    \"$project\": {"
    "      \"entityId\": \"$_id.id\","
    "      \"attrName\": \"$relAttr.k\","
    "      \"_id\": 0"
    "    }"
    "  }"
    "]",
    entityName);

  // Parse JSON to BSON
  pipeline = bson_new_from_json((const uint8_t*) pipeline_json, -1, &error);

  if (!pipeline)
    KT_RE(NULL, "Error parsing pipeline: %s\n", error.message);

  // Connection
  mongocConnectionGet(orionldState.tenantP, DbEntities);

  // Run the query
  bson_error_t          mongoError;
  mongoc_read_prefs_t*  readPrefs    = mongoc_read_prefs_new(MONGOC_READ_NEAREST);
  mongoc_cursor_t*      mongoCursorP = mongoc_collection_aggregate(orionldState.mongoc.entitiesP, MONGOC_QUERY_NONE, pipeline, NULL, readPrefs);

  if (mongoCursorP == NULL)
  {
    KT_E("Internal Error (mongoc_collection_find_with_opts ERROR)");
    mongoc_read_prefs_destroy(readPrefs);
    bson_destroy(pipeline);
    return NULL;
  }

  KjNode*        kjTypeArray        = NULL;
  KjNode*        nodeP              = NULL;
  const bson_t*  mongoDocP;

  while (mongoc_cursor_next(mongoCursorP, &mongoDocP))
  {
    char* title;
    char* detail;

    nodeP = mongocKjTreeFromBson(mongoDocP, &title, &detail);
    if (nodeP == NULL)
      KT_E("%s: %s", title, detail);
    else
    {
      if (kjTypeArray == NULL)
        kjTypeArray = kjArray(orionldState.kjsonP, NULL);

      kjChildAdd(kjTypeArray, nodeP);
    }
  }

  if (mongoc_cursor_error(mongoCursorP, &mongoError))
  {
    bson_destroy(pipeline);
    mongoc_cursor_destroy(mongoCursorP);
    mongoc_read_prefs_destroy(readPrefs);
    KT_RE(NULL, "Internal Error (DB Error '%s')", mongoError.message);
  }

  bson_destroy(pipeline);
  mongoc_cursor_destroy(mongoCursorP);
  mongoc_read_prefs_destroy(readPrefs);

  // extract infos from the mongo response to the final response format
  KjNode* typeArray = NULL;

  // create 'referencedBy' array
  typeArray = kjObject(orionldState.kjsonP, "referencedBy");
  if (typeArray == NULL)
    KT_X(1, "Internal Error (creating 'referencedBy' array): kjObject: out of memory");
  
  // fill 'referencedBy' array if we have relationships
  if (kjTypeArray != NULL)
    relExtractFromMongo(kjTypeArray, typeArray);

  return typeArray;
}
