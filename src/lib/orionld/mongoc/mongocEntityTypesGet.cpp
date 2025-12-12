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
}

#include "logMsg/logMsg.h"                                         // LM_*

#include "orionld/common/orionldState.h"                           // orionldState
#include "orionld/mongoc/mongocConnectionGet.h"                    // mongocConnectionGet
#include "orionld/mongoc/mongocKjTreeFromBson.h"                   // mongocKjTreeFromBson
#include "orionld/mongoc/mongocEntityTypesGet.h"  // Own interface



// -----------------------------------------------------------------------------
//
// FIXME: Move these two functions elsewhere
//
// They are implemented in mongoCppLegacyEntityTypesGet.cpp
// But, they don't belong there ...
//
extern void typeExtract(KjNode* regArray, KjNode* typeArray);
extern void entitiesAndPropertiesExtract(KjNode* regArray, KjNode* typeArray);



// -----------------------------------------------------------------------------
//
// mongocEntityTypesGet -
//
KjNode* mongocEntityTypesGet(bool details, const char* entityType)
{
  // We use a projection for getting all types from mongoDB together with the attributes if 'details' is on
  bson_t *pipeline = bson_new();
  bson_error_t error;

  // Pipeline-Array aufbauen
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

  pipeline = bson_new_from_json((const uint8_t *)pipeline_json, -1, &error);

  if (!pipeline) {
      LM_E(("Error parsing pipeline: %s\n", error.message));
      return NULL;
  }


  // Connection
  mongocConnectionGet(orionldState.tenantP, DbRegistrations);

  //
  // Run the query
  //
  mongoc_cursor_t*      mongoCursorP;
  bson_error_t          mongoError;
  mongoc_read_prefs_t*  readPrefs   = mongoc_read_prefs_new(MONGOC_READ_NEAREST);

  if ((mongoCursorP = mongoc_collection_aggregate(orionldState.mongoc.entitiesP, MONGOC_QUERY_NONE, pipeline, NULL, readPrefs)) == NULL)
  {
    LM_E(("Internal Error (mongoc_collection_find_with_opts ERROR)"));
    mongoc_read_prefs_destroy(readPrefs);
    bson_destroy(pipeline);
    return NULL;
  }

  KjNode*        kjRegArray        = NULL;
  KjNode*        NodeP = NULL;
  const bson_t*  mongoDocP;

  while (mongoc_cursor_next(mongoCursorP, &mongoDocP))
  {
    char* title;
    char* detail;

    NodeP = mongocKjTreeFromBson(mongoDocP, &title, &detail);
    if (NodeP == NULL)
      LM_E(("%s: %s", title, detail));
    else
    {
      if (kjRegArray == NULL)
        kjRegArray = kjArray(orionldState.kjsonP, NULL);
      kjChildAdd(kjRegArray, NodeP);
    }
  }

  if (mongoc_cursor_error(mongoCursorP, &mongoError))
  {
    LM_E(("Internal Error (DB Error '%s')", mongoError.message));
    bson_destroy(pipeline);
    mongoc_cursor_destroy(mongoCursorP);
    mongoc_read_prefs_destroy(readPrefs);
    return NULL;
  }

  bson_destroy(pipeline);
  mongoc_cursor_destroy(mongoCursorP);
  mongoc_read_prefs_destroy(readPrefs);

  // FIXME: This part has nothing to do with DB - move out from the database libs
  KjNode* typeArray = NULL;
  if (kjRegArray != NULL)
  {
    typeArray = kjArray(orionldState.kjsonP, NULL);
    if (details == false)
      typeExtract(kjRegArray, typeArray);
    else
      entitiesAndPropertiesExtract(kjRegArray, typeArray);
  }

  return typeArray;
}
