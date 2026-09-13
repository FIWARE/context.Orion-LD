/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
extern "C"
{
#include "kalloc/kaStrdup.h"                                        // kaStrdup
#include "ktrace/kTrace.h"                                          // KT_*
}

#include "orionld/types/QNode.h"                                    // QNode
#include "orionld/common/orionldState.h"                            // orionldState
#include "orionld/common/orionldError.h"                            // orionldError
#include "orionld/q/qLex.h"                                         // qLex
#include "orionld/q/qParse.h"                                       // qParse
#include "orionld/payloadCheck/pCheckGeo.h"                         // pCheckGeo
#include "orionld/payloadCheck/pCheckQueryParams.h"                 // Own interface



// ----------------------------------------------------------------------------
//
// qCheck -
//
static QNode* qCheck(char* qString)
{
  QNode* qList;
  char*  title;
  char*  detail;

  qList = qLex(qString, true, &title, &detail);
  if (qList == NULL)
  {
    orionldError(OrionldBadRequestData, "Invalid Q-Filter", detail, 400);
    KT_RE(NULL, "Error (qLex: %s: %s)", title, detail);
  }

  QNode* qNode = qParse(qList, NULL, true, true, &title, &detail);  // 3rd parameter: forDb=true
  if (qNode == NULL)
  {
    orionldError(OrionldBadRequestData, title, detail, 400);
    KT_E("Error (qParse: %s: %s) - but, the subscription will be inserted in the sub-cache without 'q'", title, detail);
  }

  return qNode;
}



// ----------------------------------------------------------------------------
//
// qApiCheck - parse 'q' a second time, into API paths instead of database paths
//
// qCheck parses 'q' for the database: "A.B" comes out as "attrs.A.md.B.value".  That tree cannot be
// matched against an Entity, and an Entity is exactly what has to be matched when the Entities may be
// split over several Context Sources - the filter can't be pushed down, so it is applied here, on the
// assembled Entity (see orionldGetEntitiesPage).
//
// Same combination the subscription cache uses for the very same purpose (see subCacheItemCompile):
// qLex without timestamp-to-float, qParse with forDb=false.
//
// The lex list is request-scoped (kalloc), so - as in qCheck - it is not released.
//
static QNode* qApiCheck(const char* qString)
{
  QNode* qList;
  char*  title;
  char*  detail;

  // qLex destroys the string it is given, and 'q' is still needed - by qCheck, right after
  char* qForLex = kaStrdup(&orionldState.kalloc, qString);

  orionldState.in.qApiModelParse = true;

  qList = qLex(qForLex, false, &title, &detail);
  if (qList == NULL)
  {
    orionldState.in.qApiModelParse = false;
    KT_RE(NULL, "Error (qLex for the API model: %s: %s)", title, detail);
  }

  QNode* qNode = qParse(qList, NULL, false, true, &title, &detail);  // forDb=false (API paths), qToDbModel=true (expand the names)

  orionldState.in.qApiModelParse = false;

  if (qNode == NULL)
    KT_RE(NULL, "Error (qParse for the API model: %s: %s)", title, detail);

  return qNode;
}



// -----------------------------------------------------------------------------
//
// pCheckQueryParams -
//
bool pCheckQueryParams
(
  char*            id,
  char*            type,
  char*            idPattern,
  char*            q,
  char*            geometry,
  char*            attrs,
  bool             local,
  EntityMap*       entityMap,
  QNode**          qNodeP,
  OrionldGeoInfo*  geoInfoP
)
{
  //
  // URI param validity check
  //

  if ((id        == NULL)  &&
      (idPattern == NULL)  &&
      (type      == NULL)  &&
      (geometry  == NULL)  &&
      (attrs     == NULL)  &&
      (q         == NULL)  &&
      (local     == false) &&
      (orionldState.in.entityMap == NULL))
  {
    orionldError(OrionldBadRequestData,
                 "Too broad query",
                 "Need at least one of: entity-id, entity-type, geo-location, attribute-list, Q-filter, local=true, or an entity map",
                 400);

    return false;
  }


  //
  // If ONE or ZERO types in URI param 'type', the prepared array isn't used, just a simple char-pointer (named "type")
  //
  if      (orionldState.in.typeList.items == 0) type = (char*) ".*";
  else if (orionldState.in.typeList.items == 1) type = orionldState.in.typeList.array[0];

  if (pCheckGeo(geoInfoP, orionldState.uriParams.geometry, orionldState.uriParams.georel, orionldState.uriParams.coordinates, orionldState.uriParams.geoproperty) == false)
    return false;

  QNode* qNode = NULL;
  if (orionldState.uriParams.q != NULL)
  {
    //
    // NOTE
    //   The API-model tree is built FIRST, and on its own copy - qCheck's qLex destroys
    //   orionldState.uriParams.q.
    //   Not on 'qCopy' though: that one is hyphens-encoded when the 'q' contains a double quote, as it
    //   is what goes out on a forwarded URL, and qLex can't read it.
    //   A failure here is not fatal: qCheck does the input validation, and without an API-model tree
    //   the assembled-Entity filter simply has no 'q' to apply.
    //
    if (orionldState.uriParams.splitEntities == true)
      orionldState.in.qNodeApi = qApiCheck(orionldState.uriParams.q);

    qNode = qCheck(orionldState.uriParams.q);
    if (qNode == NULL)
      return false;
  }

  *qNodeP = qNode;

  return true;
}
