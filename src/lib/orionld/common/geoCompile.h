#ifndef SRC_LIB_ORIONLD_COMMON_GEOCOMPILE_H_
#define SRC_LIB_ORIONLD_COMMON_GEOCOMPILE_H_

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
#include <geos_c.h>

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
}

#include "orionld/types/OrionldGeorel.h"                       // OrionldGeorel



// -----------------------------------------------------------------------------
//
// geoCompile - turn a geometry+coordinates pair into a GEOS geometry, ready to match against
//
// 'geometry' is the GeoJSON geometry type ("Point", "Polygon", ...) and 'coordinatesP' its
// coordinate array.  'what' names the owner (a subscription id, "geoQ", ...) and is used for
// the error messages only.
//
// On success, *geomP is set, and *preparedP too - except for GeorelNear, which is a distance
// calculation, not a GEOS predicate, and needs no prepared geometry.
// On failure, both are left as they were (both are NULLed out on entry).
//
extern void geoCompile
(
  const char*                   geometry,
  KjNode*                       coordinatesP,
  OrionldGeorel                 georel,
  GEOSGeometry**                geomP,
  const GEOSPreparedGeometry**  preparedP,
  const char*                   what
);

#endif  // SRC_LIB_ORIONLD_COMMON_GEOCOMPILE_H_
