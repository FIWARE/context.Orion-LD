/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include <stdlib.h>                                             // malloc, free

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
#include "orionld/troe/pgAppend.h"                             // pgAppend
#include "orionld/troe/pgQuotedString.h"                       // pgQuotedString
#include "orionld/troe/kjGeoPointExtract.h"                    // kjGeoPointExtract
#include "orionld/troe/kjGeoMultiPointExtract.h"               // kjGeoMultiPointExtract
#include "orionld/troe/kjGeoLineStringExtract.h"               // kjGeoLineStringExtract
#include "orionld/troe/kjGeoMultiLineStringExtract.h"          // kjGeoMultiLineStringExtract
#include "orionld/troe/kjGeoPolygonExtract.h"                  // kjGeoPolygonExtract
#include "orionld/troe/kjGeoMultiPolygonExtract.h"             // kjGeoMultiPolygonExtract
#include "orionld/troe/pgAttributeAppend.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// pgBufAlloc - allocate a buffer, trying kaAlloc first, then malloc
//
// Returns NULL on failure (caller must handle)
//
static char* pgBufAlloc(int size, bool* needsFree)
{
  *needsFree = false;

  char* buf = kaAlloc(&orionldState.kalloc, size);
  if (buf != NULL)
    return buf;

  buf = (char*) malloc(size);
  if (buf != NULL)
  {
    *needsFree = true;
    return buf;
  }

  KT_E("pgBufAlloc: out of memory allocating %d bytes", size);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// pgAttributeAppend -
//
// INSERT INTO attributes(instanceId,
//                        id,
//                        opMode,
//                        entityId,
//                        observedAt,
//                        subProperties,
//                        unitCode,
//                        datasetId,
//                        valueType,
//
//                        text,
//                        boolean,
//                        number,
//                        datetime,
//                        compound,
//                        geoPoint,
//                        geoPolygon,
//                        geoMultiPolygon,
//                        geoLineString,
//                        geoMultiLineString,
//                        ts) VALUES   ('', '', '', ...), ('', '', '', ...), ('', '', '', ...), ...
//
// This function appends a new ('', '', '', ...) for the VALUES of attributesBuffer
// It also calls pgSubAttributeBuild for any sub-attributes
//
void pgAttributeAppend
(
  PgAppendBuffer*  attributesBufferP,
  const char*      instanceId,
  char*            attributeName,
  const char*      opMode,
  const char*      entityId,
  char*            type,
  char*            observedAt,    // Can be NULL
  bool             subProperties,
  char*            unitCode,      // Can be NULL
  char*            datasetId,     // Can be NULL
  KjNode*          valueNodeP
)
{
  const char* comma   = (attributesBufferP->values != 0)? "," : "";
  char*       buf     = NULL;
  int         bufSize = 0;
  bool        bufNeedsFree = false;

  observedAt = (observedAt == NULL)? (char*) "null" : pgQuotedString(observedAt);
  unitCode   = (unitCode   == NULL)? (char*) "null" : pgQuotedString(unitCode);

  eqForDot(attributeName);

  if (datasetId == NULL)
    datasetId  = (char*) "None";

  const char* hasSubProperties = (subProperties == true)? "true" : "false";

  //
  // Calculate needed buffer size based on fixed parts of the SQL VALUES row.
  // The fixed overhead includes: column placeholders, quotes, commas, parentheses, null keywords, etc.
  // We add 512 bytes for this overhead plus the lengths of the known string parameters.
  //
  int fixedLen = strlen(instanceId) + strlen(attributeName) + strlen(entityId)
               + strlen(observedAt) + strlen(unitCode) + strlen(datasetId)
               + strlen(orionldState.requestTimeString) + 512;

  if (strcmp(opMode, "Delete") == 0)
  {
    bufSize = fixedLen;
    buf = pgBufAlloc(bufSize, &bufNeedsFree);
    if (buf == NULL) return;

    snprintf(buf, bufSize, "%s('%s', '%s', 'Delete', '%s', null, null, null, '%s', null, null, null, null, null, null, null, null, null, null, null, null, '%s')",
             comma, instanceId, attributeName, entityId, datasetId, orionldState.requestTimeString);
  }
  else if (type == NULL)
  {
    bufSize = fixedLen;
    buf = pgBufAlloc(bufSize, &bufNeedsFree);
    if (buf == NULL) return;

    snprintf(buf, bufSize, "%s('%s', '%s', 'Update', '%s', null, null, null, '%s', null, null, null, null, null, null, null, null, null, null, null, null, '%s')",
             comma, instanceId, attributeName, entityId, datasetId, orionldState.requestTimeString);
  }
  else if (strcmp(type, "Relationship") == 0)
  {
    if (valueNodeP->type == KjString)
    {
      bufSize = fixedLen + strlen(valueNodeP->value.s);
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'Relationship', '%s', null, null, null, null, null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, valueNodeP->value.s, orionldState.requestTimeString);
    }
    else if (valueNodeP->type == KjArray)
    {
      int    renderedValueSize = kjFastRenderSize(valueNodeP);
      char*  renderedValue     = kaAlloc(&orionldState.kalloc, renderedValueSize);

      if (renderedValue == NULL)
      {
        KT_E("pgAttributeAppend: out of memory rendering Relationship array (%d bytes)", renderedValueSize);
        return;
      }

      kjFastRender(valueNodeP, renderedValue);

      bufSize = fixedLen + renderedValueSize;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'Relationship', null, null, null, null, '%s', null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, renderedValue, orionldState.requestTimeString);
    }
    else
      KT_W("Relationships of type '%s' aren't allowed", kjValueType(valueNodeP->type));
  }
  else if (strcmp(type, "GeoProperty") == 0)
  {
    KjNode*      geoTypeNodeP     = kjLookup(valueNodeP, "type");
    KjNode*      coordinatesNodeP = kjLookup(valueNodeP, "coordinates");

    if (geoTypeNodeP == NULL || coordinatesNodeP == NULL)
    {
      KT_E("pgAttributeAppend: GeoProperty missing 'type' or 'coordinates'");
      return;
    }

    const char*  geoType          = geoTypeNodeP->value.s;
    bool         point            = (strcmp(geoType, "Point") == 0);
    char*        coordsString     = NULL;
    int          coordsStringLen  = 0;
    bool         coordsNeedsFree  = false;

    if (point == false)
    {
      // Allocate generously for coordinates - 64KB should handle most cases,
      // and the geo extract functions check bounds and return false if exceeded
      coordsStringLen = 64 * 1024;
      coordsString = pgBufAlloc(coordsStringLen, &coordsNeedsFree);
      if (coordsString == NULL)
      {
        KT_E("pgAttributeAppend: out of memory for geo coords (%d bytes)", coordsStringLen);
        return;
      }
      coordsString[0] = 0;
    }

    if (point == true)
    {
      double longitude;
      double latitude;
      double altitude;

      kjGeoPointExtract(coordinatesNodeP, &longitude, &latitude, &altitude);

      bufSize = fixedLen + 256;  // ample room for 3 doubles
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'GeoPoint', null, null, null, null, null, ST_GeomFromText('POINT(%f %f %f)', 4326), null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, longitude, latitude, altitude, orionldState.requestTimeString);
    }
    else if (strcmp(geoType, "MultiPoint") == 0)
    {
      if (kjGeoMultiPointExtract(coordinatesNodeP, coordsString, coordsStringLen) == false)
      {
        KT_E("pgAttributeAppend: kjGeoMultiPointExtract failed (coords too large?)");
        if (coordsNeedsFree) free(coordsString);
        return;
      }

      bufSize = fixedLen + strlen(coordsString) + 128;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) { if (coordsNeedsFree) free(coordsString); return; }

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'GeoMultiPoint', null, null, null, null, null, null, ST_GeomFromText('MULTIPOINT(%s)', 4326), null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, coordsString, orionldState.requestTimeString);
    }
    else if (strcmp(geoType, "LineString") == 0)
    {
      if (kjGeoLineStringExtract(coordinatesNodeP, coordsString, coordsStringLen) == false)
      {
        KT_E("pgAttributeAppend: kjGeoLineStringExtract failed (coords too large?)");
        if (coordsNeedsFree) free(coordsString);
        return;
      }

      bufSize = fixedLen + strlen(coordsString) + 128;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) { if (coordsNeedsFree) free(coordsString); return; }

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'GeoLineString', null, null, null, null, null, null, null, null, null, ST_GeomFromText('LINESTRING(%s)', 4326), null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, coordsString, orionldState.requestTimeString);
    }
    else if (strcmp(geoType, "MultiLineString") == 0)
    {
      if (kjGeoMultiLineStringExtract(coordinatesNodeP, coordsString, coordsStringLen) == false)
      {
        KT_E("pgAttributeAppend: kjGeoMultiLineStringExtract failed (coords too large?)");
        if (coordsNeedsFree) free(coordsString);
        return;
      }

      bufSize = fixedLen + strlen(coordsString) + 128;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) { if (coordsNeedsFree) free(coordsString); return; }

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'GeoMultiLineString', null, null, null, null, null, null, null, null, null, null, ST_GeomFromText('MULTILINESTRING(%s)', 4326), '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, coordsString, orionldState.requestTimeString);
    }
    else if (strcmp(geoType, "Polygon") == 0)
    {
      if (kjGeoPolygonExtract(coordinatesNodeP, coordsString, coordsStringLen) == false)
      {
        KT_E("pgAttributeAppend: kjGeoPolygonExtract failed (coords too large?)");
        if (coordsNeedsFree) free(coordsString);
        return;
      }

      bufSize = fixedLen + strlen(coordsString) + 128;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) { if (coordsNeedsFree) free(coordsString); return; }

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'GeoPolygon', null, null, null, null, null, null, null, ST_GeomFromText('POLYGON(%s)', 4326), null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, coordsString, orionldState.requestTimeString);
    }
    else if (strcmp(geoType, "MultiPolygon") == 0)
    {
      if (kjGeoMultiPolygonExtract(coordinatesNodeP, coordsString, coordsStringLen) == false)
      {
        KT_E("pgAttributeAppend: kjGeoMultiPolygonExtract failed (coords too large?)");
        if (coordsNeedsFree) free(coordsString);
        return;
      }

      bufSize = fixedLen + strlen(coordsString) + 128;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) { if (coordsNeedsFree) free(coordsString); return; }

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'GeoMultiPolygon', null, null, null, null, null, null, null, null, ST_GeomFromText('MULTIPOLYGON(%s)', 4326), null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, coordsString, orionldState.requestTimeString);
    }

    // Free coords buffer if it was malloc'd (not for kaAlloc'd or point case)
    if (coordsNeedsFree && coordsString != NULL)
      free(coordsString);
  }
  else  // Property OR JsonProperty
  {
    if (valueNodeP->type == KjString)
    {
      int neededSize = fixedLen + strlen(valueNodeP->value.s);

      buf = pgBufAlloc(neededSize, &bufNeedsFree);
      if (buf == NULL) return;
      bufSize = neededSize;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'String', '%s', null, null, null, null, null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, valueNodeP->value.s, orionldState.requestTimeString);
    }
    else if (valueNodeP->type == KjBoolean)
    {
      const char* value = (valueNodeP->value.b == true)? "true" : "false";

      bufSize = fixedLen;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'Boolean', null, %s, null, null, null, null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, value, orionldState.requestTimeString);
    }
    else if (valueNodeP->type == KjInt)
    {
      bufSize = fixedLen + 32;  // room for int64
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'Number', null, null, %lld, null, null, null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, valueNodeP->value.i, orionldState.requestTimeString);
    }
    else if (valueNodeP->type == KjFloat)
    {
      bufSize = fixedLen + 32;  // room for double
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'Number', null, null, %f, null, null, null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, valueNodeP->value.f, orionldState.requestTimeString);
    }
    else if ((valueNodeP->type == KjArray) || (valueNodeP->type == KjObject))
    {
      if (strcmp(type, "JsonProperty") == 0)
      {
        KT_W("TRoE for Compound JsonProperty still to be implemented");
        return;
      }

      int   renderedValueSize = kjFastRenderSize(valueNodeP);
      char* renderedValue     = kaAlloc(&orionldState.kalloc, renderedValueSize);

      if (renderedValue == NULL)
      {
        // kaAlloc failed - try malloc as fallback
        renderedValue = (char*) malloc(renderedValueSize);
        if (renderedValue == NULL)
        {
          KT_E("pgAttributeAppend: out of memory rendering compound value (%d bytes)", renderedValueSize);
          return;
        }
        orionldStateDelayedFreeEnqueue(renderedValue);
      }

      kjFastRender(valueNodeP, renderedValue);

      bufSize = fixedLen + renderedValueSize;
      buf = pgBufAlloc(bufSize, &bufNeedsFree);
      if (buf == NULL) return;

      snprintf(buf, bufSize, "%s('%s', '%s', '%s', '%s', %s, %s, %s, '%s', 'Compound', null, null, null, null, '%s', null, null, null, null, null, null, '%s')",
               comma, instanceId, attributeName, opMode, entityId, observedAt, hasSubProperties, unitCode, datasetId, renderedValue, orionldState.requestTimeString);
    }
  }

  if ((buf == NULL) || (buf[0] == 0))
  {
    KT_W("TROE: too big attribute value? (nothing written to history DB)");
    if (bufNeedsFree && buf != NULL) free(buf);
    return;
  }

  pgAppend(attributesBufferP, buf, 0);
  attributesBufferP->values += 1;

  if (bufNeedsFree)
    free(buf);
}
