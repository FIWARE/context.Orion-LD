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
* Author: Carsten Frey
*/
#include <bson/bson.h>                                             // bson_t, ...
#include <mongoc/mongoc.h>                                         // MongoDB C Client Driver


extern "C"
{
#include "kjson/KjNode.h"                                          // KjNode
#include "kjson/kjBuilder.h"                                       // kjArray
#include "ktrace/kTrace.h"                                         // trace messages -
#include "kjson/kjLookup.h"                                        // kjLookup
}

#include "logMsg/logMsg.h"                                         // LM_*

#include "orionld/common/orionldState.h"                           // orionldState
#include "orionld/mongoc/mongocConnectionGet.h"                    // mongocConnectionGet
#include "orionld/mongoc/mongocKjTreeFromBson.h"                   // mongocKjTreeFromBson
#include "orionld/mongoc/mongocEntityTypesGet.h"  // Own interface
#include "orionld/context/orionldContextItemAliasLookup.h"             // orionldContextItemAliasLookup


// -----------------------------------------------------------------------------
//
// typeExtractFromMongo -
//
void typeExtractFromMongo(KjNode* inputArray, KjNode* typeArray)
{
  for (KjNode* arrItemP = inputArray->value.firstChildP; arrItemP != NULL; arrItemP = arrItemP->next)
  {
    KjNode* tNode = kjLookup(arrItemP, "_id");

    if (tNode == NULL)
    {
      KT_W("No _id found in tree ...");
      continue;
    }

    if (tNode != NULL)
    {
      kjChildAdd(typeArray, tNode);
      // Lookup alias for type name in context
      tNode->value.s = orionldContextItemAliasLookup(orionldState.contextP, tNode->value.s, NULL, NULL);
    }
  }
}



// -----------------------------------------------------------------------------
//
// typeAndAttrsExtractFromMongo -
//
void typeAndAttrsExtractFromMongo(KjNode* inputArray, KjNode* typeArray)
{
  for (KjNode* arrItemP = inputArray->value.firstChildP; arrItemP != NULL; arrItemP = arrItemP->next)
  {
    KjNode* idP = kjLookup(arrItemP, "_id");
    KjNode* attrP    = kjLookup(arrItemP, "attrs");
    KjNode* nodeResponseP = kjObject(orionldState.kjsonP, NULL);

    if (idP != NULL)
    {
      KjNode* idNodeP  = kjString(orionldState.kjsonP, "id", idP->value.s);
      kjChildAdd(nodeResponseP, idNodeP);
      KjNode* typeP = kjString(orionldState.kjsonP, "typeName", orionldContextItemAliasLookup(orionldState.contextP, idP->value.s, NULL, NULL));
      kjChildAdd(nodeResponseP, typeP);
    }

    if (attrP != NULL)
    {
      // loop over all attributes and add to response
      KjNode* attribP  = kjArray(orionldState.kjsonP, "attributeNames");

      for (KjNode* attrItemP = attrP->value.firstChildP; attrItemP != NULL; attrItemP = attrItemP->next)
      {
        // lookup alias for attribute name in context
        KjNode* arrNodeP  = kjString(orionldState.kjsonP, NULL, orionldContextItemAliasLookup(orionldState.contextP, attrItemP->value.s, NULL, NULL));
        kjChildAdd(attribP, arrNodeP);
      }

      kjChildAdd(nodeResponseP, attribP);
    }

    kjChildAdd(typeArray, nodeResponseP);
  }
}



// -----------------------------------------------------------------------------
//
// mongocEntityTypesGet -
//
KjNode* mongocEntityTypesGet(bool details, const char* entityType)
{
  //
  // We use a projection for getting all types from mongoDB together with the attributes
  // if details == true we will return also the attributes for each type
  //
  bson_t*       pipeline = bson_new();
  bson_error_t  error;

  // Pipeline-Array in JSON-Format
  const char *pipeline_json =
    "["
    "  {"
    "    \"$project\": {"
    "      \"type\": {"
    "        \"$ifNull\": [\"$_id.type\", null]"
    "      },"
    "      \"attrNames\": 1"
    "    }"
    "  },"
    "  {"
    "    \"$project\": {"
    "      \"attrNames\": {"
    "        \"$cond\": {"
    "          \"if\": {\"$eq\": [\"$attrNames\", []]},"
    "          \"then\": [null],"
    "          \"else\": \"$attrNames\""
    "        }"
    "      },"
    "      \"type\": 1"
    "    }"
    "  },"
    "  {"
    "    \"$unwind\": \"$attrNames\""
    "  },"
    "  {"
    "    \"$group\": {"
    "      \"_id\": {"
    "        \"$cond\": {"
    "          \"if\": {\"$in\": [\"$type\", [null, \"\"]]},"
    "          \"then\": \"\","
    "          \"else\": \"$type\""
    "        }"
    "      },"
    "      \"attrs\": {\"$addToSet\": \"$attrNames\"}"
    "    }"
    "  },"
    "  {"
    "    \"$sort\": {\"_id\": 1}"
    "  }"
    "]";

  // Parse JSON to BSON
  pipeline = bson_new_from_json((const uint8_t*) pipeline_json, -1, &error);

  if (!pipeline)
    KT_RE(NULL, "Error parsing pipeline: %s\n", error.message);

  // Connection
  mongocConnectionGet(orionldState.tenantP, DbEntities);

  //
  // Run the query
  //
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
    KT_E("Internal Error (DB Error '%s')", mongoError.message);
    bson_destroy(pipeline);
    mongoc_cursor_destroy(mongoCursorP);
    mongoc_read_prefs_destroy(readPrefs);
    return NULL;
  }

  bson_destroy(pipeline);
  mongoc_cursor_destroy(mongoCursorP);
  mongoc_read_prefs_destroy(readPrefs);

  // extract infos from the mongo response to the final response format
  KjNode* typeArray = NULL;
  if (kjTypeArray != NULL)
  {
    typeArray = kjArray(orionldState.kjsonP, NULL);

    if (details == false)
      typeExtractFromMongo(kjTypeArray, typeArray);
    else
      typeAndAttrsExtractFromMongo(kjTypeArray, typeArray);
  }

  return typeArray;
}
