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
#include <stdio.h>                                             // snprintf
#include <geos_c.h>

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjRender.h"                                    // kjFastRender
}

#include "orionld/types/OrionldGeorel.h"                       // OrionldGeorel, GeorelNear
#include "orionld/common/geosInit.h"                           // geosHandle
#include "orionld/common/geoCompile.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// geoCompile -
//
void geoCompile
(
  const char*                   geometry,
  KjNode*                       coordinatesP,
  OrionldGeorel                 georel,
  GEOSGeometry**                geomP,
  const GEOSPreparedGeometry**  preparedP,
  const char*                   what
)
{
  *geomP     = NULL;
  *preparedP = NULL;

  if ((geometry == NULL) || (coordinatesP == NULL))
    return;

  if (geosHandle == NULL)
    KT_RVE("%s: GEOS is not initialized - no geo-matching will be done", what);

  //
  // Build the GeoJSON string GEOS wants: {"type":"<geometry>","coordinates":<coords>}
  //
  char geoJson[2048];
  char coordsBuf[1536];

  kjFastRender(coordinatesP, coordsBuf);

  int len = snprintf(geoJson, sizeof(geoJson), "{\"type\":\"%s\",\"coordinates\":%s}", geometry, coordsBuf);

  if ((len <= 0) || (len >= (int) sizeof(geoJson)))
    KT_RVE("%s: coordinates too big for the GEOS reader (%d bytes)", what, len);

  GEOSGeoJSONReader* reader = GEOSGeoJSONReader_create_r(geosHandle);

  *geomP = GEOSGeoJSONReader_readGeometry_r(geosHandle, reader, geoJson);
  GEOSGeoJSONReader_destroy_r(geosHandle, reader);

  //
  // GeorelNear is a distance calculation (haversine), not a GEOS predicate - it needs no prepared geometry
  //
  if ((*geomP != NULL) && (georel != GeorelNear))
    *preparedP = GEOSPrepare_r(geosHandle, *geomP);
}
