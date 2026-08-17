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
#include <string.h>                                             // strlen, strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kalloc/kaAlloc.h"                                    // kaAlloc
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjRenderSize.h"                                // kjFastRenderSize
#include "kjson/kjRender.h"                                    // kjFastRender
}

#include "orionld/types/PgAppendBuffer.h"                      // PgAppendBuffer
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/eqForDot.h"                           // eqForDot
#include "orionld/common/correlatorGet.h"                      // correlatorGet
#include "orionld/troe/pgAppend.h"                             // pgAppend
#include "orionld/troe/pgQuotedString.h"                       // pgQuotedString
#include "orionld/troe/kjGeoPointExtract.h"                    // kjGeoPointExtract
#include "orionld/troe/kjGeoMultiPointExtract.h"               // kjGeoMultiPointExtract
#include "orionld/troe/kjGeoLineStringExtract.h"               // kjGeoLineStringExtract
#include "orionld/troe/kjGeoMultiLineStringExtract.h"          // kjGeoMultiLineStringExtract
#include "orionld/troe/kjGeoPolygonExtract.h"                  // kjGeoPolygonExtract
#include "orionld/troe/kjGeoMultiPolygonExtract.h"             // kjGeoMultiPolygonExtract
#include "orionld/troe/pgSubAttributeAppend.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// pgBufAllocSub - allocate a buffer via kaAlloc
//
// Returns NULL on failure (caller must handle)
//
static char* pgBufAllocSub(int size)
{
  char* buf = kaAlloc(&orionldState.kalloc, size);

  if (buf == NULL)
    KT_E("pgBufAllocSub: out of memory allocating %d bytes", size);

  return buf;
}



// -----------------------------------------------------------------------------
//
// pgSubAttributeAppend -
//
// INSERT INTO subAttributes(instanceId,
//                           id,
//                           entityId,
//                           attrInstanceId,
//                           attrDatasetId,
//                           observedAt,
//                           unitCode,
//                           valueType,
//
//                           text,
//                           boolean,
//                           number,
//                           datetime,
//                           compound,
//                           geoPoint,
//                           geoMultiPoint,
//                           geoPolygon,
//                           geoMultiPolygon,
//                           geoLineString,
//                           geoMultiLineString,
//                           ts) VALUES   ('', '', '', ...), ('', '', '', ...), ('', '', '', ...), ...
//
// This function appends a new ('', '', '', ...) for the VALUES of subAttributesBuffer
//
void pgSubAttributeAppend
(
  PgAppendBuffer*  subAttributesBufferP,
  const char*      instanceId,
  char*            subAttributeName,
  const char*      entityId,
  const char*      attrInstanceId,
  char*            attrDatasetId,  // might be NULL, but can't be in the DB
  const char*      type,
  char*            observedAt,     // Can be NULL
  char*            unitCode,       // Can be NULL
  KjNode*          valueNodeP,
  const char*      object
)
{
  const char* comma   = (subAttributesBufferP->values != 0)? "," : "";
  char*       buf     = NULL;
  int         bufSize = 0;

  eqForDot(subAttributeName);

  attrDatasetId = (attrDatasetId == NULL)? (char*) "'None'" : pgQuotedString(attrDatasetId);
  observedAt    = (observedAt    == NULL)? (char*) "null"   : pgQuotedString(observedAt);
  unitCode      = (unitCode      == NULL)? (char*) "null"   : pgQuotedString(unitCode);

  // The correlator is client-influenced - escape it (correlatorGet already strips any quote, pgQuotedString adds the quotes)
  char* correlator = pgQuotedString(correlatorGet());

  // Calculate fixed overhead for the SQL VALUES row
  int fixedLen = strlen(instanceId) + strlen(subAttributeName) + strlen(entityId)
               + strlen(attrInstanceId) + strlen(attrDatasetId)
               + strlen(observedAt) + strlen(unitCode)
               + strlen(orionldState.requestTimeString) + strlen(correlator) + 512;

  //
  // ⚠️ Every branch below reads valueNodeP (or, for a Relationship, object), and
  // none of them checks it. A caller with nothing to store is a caller with a bug,
  // but the answer to that is a log line, not a SEGV in a request thread - which is
  // exactly what a PATCH deleting a sub-attribute used to produce.
  //
  // Note that a deletion cannot be recorded here at all: 'valueType' is a postgres
  // ENUM without a "Delete" member, and unlike 'attributes' the 'subAttributes'
  // table has no opMode column to put one in. That is why the caller does not call
  // us for a deletion - see troePatchEntity2.
  //
  if ((valueNodeP == NULL) && (object == NULL) && ((type == NULL) || (strcmp(type, "GeoProperty") != 0)))
    KT_RVE("pgSubAttributeAppend: no value and no object for sub-attribute '%s' of entity '%s' (type '%s') - nothing to store",
           subAttributeName, entityId, (type != NULL)? type : "None");

  if (type == NULL)
  {
    bufSize = fixedLen + (object != NULL ? strlen(object) : 0);
    buf = pgBufAllocSub(bufSize);
    if (buf == NULL) return;

    snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'UnchangedType', '%s', null, null, null, null, null, null, null, null, null, null, '%s', %s)",
             comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, object, orionldState.requestTimeString, correlator);
  }
  else if (strcmp(type, "Relationship") == 0)
  {
    bufSize = fixedLen + (object != NULL ? strlen(object) : 0);
    buf = pgBufAllocSub(bufSize);
    if (buf == NULL) return;

    snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'Relationship', '%s', null, null, null, null, null, null, null, null, null, null, '%s', %s)",
             comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, object, orionldState.requestTimeString, correlator);
  }
  else if (strcmp(type, "GeoProperty") == 0)
  {
    KjNode*     geoTypeNodeP     = kjLookup(valueNodeP, "type");
    KjNode*     coordinatesNodeP = kjLookup(valueNodeP, "coordinates");

    if (geoTypeNodeP == NULL || coordinatesNodeP == NULL)
    {
      KT_E("pgSubAttributeAppend: GeoProperty missing 'type' or 'coordinates'");
      return;
    }

    const char* geoType = geoTypeNodeP->value.s;

    if (strcmp(geoType, "Point") == 0)
    {
      double longitude;
      double latitude;
      double altitude;

      kjGeoPointExtract(coordinatesNodeP, &longitude, &latitude, &altitude);

      bufSize = fixedLen + 256;
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'GeoPoint', null, null, null, null, null, ST_GeomFromText('POINT(%f %f %f)', 4326), null, null, null, null, null, '%s', %s)",
               comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, longitude, latitude, altitude, orionldState.requestTimeString, correlator);
    }
    else
    {
      // Use malloc for large geo coord buffers - kaAlloc pool blocks (64KB) can't serve 64KB requests
      int   coordsLen = 64 * 1024;
      char* coordsString = (char*) malloc(coordsLen);

      if (coordsString == NULL)
      {
        KT_E("pgSubAttributeAppend: out of memory for geo coords (%d bytes)", coordsLen);
        return;
      }
      orionldStateDelayedFreeEnqueue(coordsString);
      coordsString[0] = 0;

      bool extractOk = false;
      const char* geoTypeName = NULL;
      const char* stPrefix    = NULL;

      if (strcmp(geoType, "MultiPoint") == 0)
      {
        extractOk   = kjGeoMultiPointExtract(coordinatesNodeP, coordsString, coordsLen);
        geoTypeName = "GeoMultiPoint";
        stPrefix    = "MULTIPOINT";
      }
      else if (strcmp(geoType, "LineString") == 0)
      {
        extractOk   = kjGeoLineStringExtract(coordinatesNodeP, coordsString, coordsLen);
        geoTypeName = "GeoLineString";
        stPrefix    = "LINESTRING";
      }
      else if (strcmp(geoType, "MultiLineString") == 0)
      {
        extractOk   = kjGeoMultiLineStringExtract(coordinatesNodeP, coordsString, coordsLen);
        geoTypeName = "GeoMultiLineString";
        stPrefix    = "MULTILINESTRING";
      }
      else if (strcmp(geoType, "Polygon") == 0)
      {
        extractOk   = kjGeoPolygonExtract(coordinatesNodeP, coordsString, coordsLen);
        geoTypeName = "GeoPolygon";
        stPrefix    = "POLYGON";
      }
      else if (strcmp(geoType, "MultiPolygon") == 0)
      {
        extractOk   = kjGeoMultiPolygonExtract(coordinatesNodeP, coordsString, coordsLen);
        geoTypeName = "GeoMultiPolygon";
        stPrefix    = "MULTIPOLYGON";
      }

      if (!extractOk || geoTypeName == NULL)
      {
        KT_E("pgSubAttributeAppend: geo extraction failed for type '%s'", geoType);
        return;
      }

      bufSize = fixedLen + strlen(coordsString) + strlen(stPrefix) + 256;
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      //
      // Build the SQL with the correct geo column position
      // Column order: geoPoint(14), geoMultiPoint(15), geoPolygon(16), geoMultiPolygon(17), geoLineString(18), geoMultiLineString(19)
      //
      if (strcmp(geoType, "MultiPoint") == 0)
      {
        snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', null, null, null, null, null, null, ST_GeomFromText('%s(%s)', 4326), null, null, null, null, '%s', %s)",
                 comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, geoTypeName, stPrefix, coordsString, orionldState.requestTimeString, correlator);
      }
      else if (strcmp(geoType, "LineString") == 0)
      {
        snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', null, null, null, null, null, null, null, null, null, ST_GeomFromText('%s(%s)', 4326), null, '%s', %s)",
                 comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, geoTypeName, stPrefix, coordsString, orionldState.requestTimeString, correlator);
      }
      else if (strcmp(geoType, "MultiLineString") == 0)
      {
        snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', null, null, null, null, null, null, null, null, null, null, ST_GeomFromText('%s(%s)', 4326), '%s', %s)",
                 comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, geoTypeName, stPrefix, coordsString, orionldState.requestTimeString, correlator);
      }
      else if (strcmp(geoType, "Polygon") == 0)
      {
        snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', null, null, null, null, null, null, null, ST_GeomFromText('%s(%s)', 4326), null, null, null, '%s', %s)",
                 comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, geoTypeName, stPrefix, coordsString, orionldState.requestTimeString, correlator);
      }
      else if (strcmp(geoType, "MultiPolygon") == 0)
      {
        snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', null, null, null, null, null, null, null, null, ST_GeomFromText('%s(%s)', 4326), null, null, '%s', %s)",
                 comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, geoTypeName, stPrefix, coordsString, orionldState.requestTimeString, correlator);
      }

    }
  }
  else  // Property
  {
    if (valueNodeP->type == KjString)
    {
      bufSize = fixedLen + strlen(valueNodeP->value.s);
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'String', '%s', null, null, null, null, null, null, null, null, null, null, '%s', %s)",
               comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, valueNodeP->value.s, orionldState.requestTimeString, correlator);
    }
    else if (valueNodeP->type == KjBoolean)
    {
      const char* value = (valueNodeP->value.b == true)? "true" : "false";

      bufSize = fixedLen;
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'Boolean', null, %s, null, null, null, null, null, null, null, null, null, '%s', %s)",
               comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, value, orionldState.requestTimeString, correlator);
    }
    else if (valueNodeP->type == KjInt)
    {
      bufSize = fixedLen + 32;
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'Number', null, null, %lld, null, null, null, null, null, null, null, null, '%s', %s)",
               comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, valueNodeP->value.i, orionldState.requestTimeString, correlator);
    }
    else if (valueNodeP->type == KjFloat)
    {
      bufSize = fixedLen + 32;
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'Number', null, null, %f, null, null, null, null, null, null, null, null, '%s', %s)",
               comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, valueNodeP->value.f, orionldState.requestTimeString, correlator);
    }
    else if ((valueNodeP->type == KjArray) || (valueNodeP->type == KjObject))
    {
      // WARNING: If a sub-attribute is HUGE, it may not have room enough in a buffer allocated by kaAlloc (there's a max-size)
      int   renderedValueSize = kjFastRenderSize(valueNodeP);
      char* renderedValue     = kaAlloc(&orionldState.kalloc, renderedValueSize);

      // if kaAlloc returns null-pointer, the sub-attribute is too big -> report error and return
      if (renderedValue == NULL)
      {
        KT_E("error allocating %d bytes for sub-attribute value", renderedValueSize);
        return;
      }

      kjFastRender(valueNodeP, renderedValue);

      bufSize = fixedLen + renderedValueSize;
      buf = pgBufAllocSub(bufSize);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, 'Compound', null, null, null, null, '%s', null, null, null, null, null, null, '%s', %s)",
               comma, instanceId, subAttributeName, entityId, attrInstanceId, attrDatasetId, observedAt, unitCode, renderedValue, orionldState.requestTimeString, correlator);
    }
  }

  if (buf == NULL || buf[0] == 0)
  {
    KT_W("TROE: sub-attribute not written to history DB (allocation failure or empty)");
    return;
  }

  pgAppend(subAttributesBufferP, buf, 0);
  subAttributesBufferP->values += 1;
}
