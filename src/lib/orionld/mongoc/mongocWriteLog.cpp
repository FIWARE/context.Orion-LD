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
#include <bson/bson.h>                                           // bson_t, ...

extern "C"
{
#include "ktrace/ktOut.h"                                        // ktOut
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/common/fileName.h"                             // fileName
#include "orionld/mongoc/mongocWriteLog.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// mongocWriteLog -
//
void mongocWriteLog
(
  const char*  msg,
  const char*  dbName,
  const char*  collectionName,
  bson_t*      selectorP,
  bson_t*      requestP,
  const char*  path,
  int          lineNo,
  const char*  functionName,
  int          traceLevel
)
{
  char* fileNameOnly = fileName(path);

  char line[2048];

  snprintf(line, sizeof(line), "---------- %s ----------", msg);
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);

  snprintf(line, sizeof(line), "  * Database Name:         '%s'", dbName);
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);

  snprintf(line, sizeof(line), "  * Collection Name:       '%s'", collectionName);
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);

  if (selectorP != NULL)
  {
    char* selector = bson_as_legacy_extended_json(selectorP, NULL);

    snprintf(line, sizeof(line), "  * Selector:              '%s'", selector);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);
    bson_free(selector);
  }

  if (requestP != NULL)
  {
    char* request = bson_as_legacy_extended_json(requestP, NULL);

    snprintf(line, sizeof(line), "  * Request:               '%s'", request);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);
    bson_free(request);
  }
}



// -----------------------------------------------------------------------------
//
// mongocReadLog -
//
void mongocReadLog
(
  const char*  msg,
  const char*  dbName,
  const char*  collectionName,
  bson_t*      filterP,
  bson_t*      optionsP,
  const char*  path,
  int          lineNo,
  const char*  functionName,
  int          traceLevel
)
{
  char* fileNameOnly = fileName(path);

  char line[2048];

  snprintf(line, sizeof(line), "---------- %s ----------", msg);
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);

  snprintf(line, sizeof(line), "  * Database Name:         '%s'", dbName);
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);

  snprintf(line, sizeof(line), "  * Collection Name:       '%s'", collectionName);
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);


  if (filterP != NULL)
  {
    char* filter = bson_as_legacy_extended_json(filterP, NULL);
    snprintf(line, sizeof(line), "  * Filter:                '%s'", filter);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);
    bson_free(filter);
  }

  if (optionsP != NULL)
  {
    char* options = bson_as_legacy_extended_json(optionsP, NULL);
    snprintf(line, sizeof(line), "  * Options:             '%s'", options);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, line);
    bson_free(options);
  }
}
