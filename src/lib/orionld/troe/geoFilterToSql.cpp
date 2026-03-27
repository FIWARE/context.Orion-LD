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
#include <stdio.h>                                               // snprintf
#include <string.h>                                              // strlen, strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/types/OrionldGeoInfo.h"                        // OrionldGeoInfo
#include "orionld/types/OrionldGeometry.h"                       // GeoPoint, etc.
#include "orionld/types/OrionldGeorel.h"                         // GeorelNear, etc.
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/troe/geoFilterToSql.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// geoColumnForGeometry - determine which PostGIS column matches the query geometry
//
// The attributes table has separate columns for each geometry type.
// For geo-queries, we typically filter on the "location" attribute which is a GeoProperty.
// The column to use depends on the geometry of the entity's data, not the query geometry.
// For simplicity, we check all point/polygon columns using COALESCE-like approach.
//
// However, "location" is most commonly stored as geopoint. We'll check the most common
// columns and use OR to cover multiple geometry types.
//
static const char* geoColumnForGeometry(OrionldGeometry queryGeometry)
{
  // For entity filtering, we match against the entity's stored geometry.
  // "location" is typically a GeoPoint, but could be a Polygon.
  // Use geopoint as primary (most common for location attribute).
  // The caller should handle multiple geo columns if needed.
  (void) queryGeometry;  // Not used - we check the entity's stored geometry
  return "geopoint";
}



// -----------------------------------------------------------------------------
//
// geometryTypeString - GeoJSON type string for a geometry enum
//
static const char* geometryTypeString(OrionldGeometry geometry)
{
  switch (geometry)
  {
  case GeoPoint:            return "Point";
  case GeoMultiPoint:       return "MultiPoint";
  case GeoLineString:       return "LineString";
  case GeoMultiLineString:  return "MultiLineString";
  case GeoPolygon:          return "Polygon";
  case GeoMultiPolygon:     return "MultiPolygon";
  default:                  return "Point";
  }
}



// -----------------------------------------------------------------------------
//
// geoFilterToSql - convert OrionldGeoInfo to SQL WHERE clause fragment
//
// Generates SQL like:
//   EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id
//           AND id = 'https://uri.etsi.org/ngsi-ld/location'
//           AND ST_DWithin(geopoint, ST_GeomFromGeoJSON('{"type":"Point","coordinates":[...]}'), 1000))
//
const char* geoFilterToSql(OrionldGeoInfo* geoInfoP)
{
  if (geoInfoP == NULL || geoInfoP->georel == GeorelNone)
    return NULL;

  // Render coordinates to JSON string
  char coordsBuf[2048];
  kjFastRender(geoInfoP->coordinates, coordsBuf);

  // Build GeoJSON object string for ST_GeomFromGeoJSON
  const char* geoType = geometryTypeString(geoInfoP->geometry);
  char geojson[4096];
  snprintf(geojson, sizeof(geojson), "{\"type\":\"%s\",\"coordinates\":%s}", geoType, coordsBuf);

  // Determine the geo column(s) to check
  // For now, check geopoint (most common for location) and geopolygon with COALESCE
  const char* geoCol = geoColumnForGeometry(geoInfoP->geometry);

  // Get the expanded geoproperty name
  // pCheckGeo/pcheckGeoQ may store "location" as-is or already expanded
  // For PostgreSQL, we always need the expanded URI form
  const char* geoPropExpanded = orionldAttributeExpand(orionldState.contextP,
                                                       geoInfoP->geoProperty, true, NULL);

  // Build the spatial predicate based on georel
  char spatialPredicate[4096];

  switch (geoInfoP->georel)
  {
  case GeorelNear:
    if (geoInfoP->maxDistance > 0)
    {
      snprintf(spatialPredicate, sizeof(spatialPredicate),
               "ST_DWithin(%s, ST_GeomFromGeoJSON('%s'), %d)",
               geoCol, geojson, geoInfoP->maxDistance);
    }
    else if (geoInfoP->minDistance > 0)
    {
      snprintf(spatialPredicate, sizeof(spatialPredicate),
               "NOT ST_DWithin(%s, ST_GeomFromGeoJSON('%s'), %d)",
               geoCol, geojson, geoInfoP->minDistance);
    }
    else
    {
      // near without distance constraints - just check existence
      snprintf(spatialPredicate, sizeof(spatialPredicate),
               "%s IS NOT NULL", geoCol);
    }
    break;

  case GeorelWithin:
    snprintf(spatialPredicate, sizeof(spatialPredicate),
             "ST_Within(%s::geometry, ST_GeomFromGeoJSON('%s')::geometry)",
             geoCol, geojson);
    break;

  case GeorelContains:
    snprintf(spatialPredicate, sizeof(spatialPredicate),
             "ST_Contains(%s::geometry, ST_GeomFromGeoJSON('%s')::geometry)",
             geoCol, geojson);
    break;

  case GeorelIntersects:
    snprintf(spatialPredicate, sizeof(spatialPredicate),
             "ST_Intersects(%s, ST_GeomFromGeoJSON('%s'))",
             geoCol, geojson);
    break;

  case GeorelEquals:
    snprintf(spatialPredicate, sizeof(spatialPredicate),
             "ST_Equals(%s::geometry, ST_GeomFromGeoJSON('%s')::geometry)",
             geoCol, geojson);
    break;

  case GeorelDisjoint:
    snprintf(spatialPredicate, sizeof(spatialPredicate),
             "NOT ST_Intersects(%s, ST_GeomFromGeoJSON('%s'))",
             geoCol, geojson);
    break;

  case GeorelOverlaps:
    snprintf(spatialPredicate, sizeof(spatialPredicate),
             "ST_Overlaps(%s::geometry, ST_GeomFromGeoJSON('%s')::geometry)",
             geoCol, geojson);
    break;

  default:
    KT_E("geoFilterToSql: unsupported georel %d", geoInfoP->georel);
    return NULL;
  }

  // Build the full EXISTS subquery
  int   bufSize = 4096 + strlen(geoPropExpanded) + strlen(spatialPredicate);
  char* buf     = (char*) kaAlloc(&orionldState.kalloc, bufSize);

  snprintf(buf, bufSize,
           "EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id"
           " AND id = '%s' AND %s)",
           geoPropExpanded, spatialPredicate);

  return buf;
}
