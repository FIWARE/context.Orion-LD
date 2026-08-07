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
#include <stdio.h>                                               // snprintf
#include <geos_c.h>                                              // GEOSGeoJSONReader, GEOSPrepare

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/types/OrionldGeoInfo.h"                        // OrionldGeoInfo
#include "orionld/types/OrionldGeorel.h"                         // GeorelNear
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/common/geosInit.h"                             // geosHandle
#include "orionld/payloadCheck/pcheckGeoQ.h"                     // pcheckGeoQ
#include "orionld/subCache/subCacheItemGeoCompile.h"             // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemGeoCompile -
//
void subCacheItemGeoCompile(SubCacheItem* sciP, KjNode* geoqP)
{
  //
  // pcheckGeoQ hands back an OrionldGeoInfo that points INTO the tree it is given,
  // and it rewrites the "geoproperty" it finds there into request-scoped memory.
  // Neither is acceptable for the cached subTree, so it gets a private clone of
  // "geoQ" to chew on - a clone that the cache item then owns.
  //
  sciP->geoQP   = kjClone(NULL, geoqP);
  sciP->geoInfo = pcheckGeoQ(NULL, sciP->geoQP, true);

  if (sciP->geoInfo == NULL)
    KT_RVE("Sub '%s': invalid 'geoQ' - the subscription will not geo-match", sciP->subId);

  //
  // pcheckGeoQ may hand back a geoProperty with '=' instead of '.' (dotForEq from an
  // earlier payload check). The geoProperty is a strdup'd copy, safe to fix in place.
  //
  if (sciP->geoInfo->geoProperty != NULL)
    eqForDot(sciP->geoInfo->geoProperty);

  //
  // If the coordinates came as a String, pcheckGeoQ parsed them into request-scoped
  // memory - the cache needs its own copy.
  //
  KjNode* clonedCoordinatesP = kjLookup(sciP->geoQP, "coordinates");
  if ((clonedCoordinatesP != NULL) && (clonedCoordinatesP->type == KjString))
    sciP->geoInfo->coordinates = kjClone(NULL, sciP->geoInfo->coordinates);

  KjNode* geometryP    = kjLookup(sciP->geoQP, "geometry");
  KjNode* coordinatesP = sciP->geoInfo->coordinates;

  if ((geometryP == NULL) || (coordinatesP == NULL))
    return;

  if (geosHandle == NULL)
    KT_RVE("Sub '%s': GEOS is not initialized - the subscription will not geo-match", sciP->subId);

  //
  // Build the GeoJSON string GEOS wants: {"type":"<geometry>","coordinates":<coords>}
  //
  char geoJson[2048];
  char coordsBuf[1536];

  kjFastRender(coordinatesP, coordsBuf);

  int len = snprintf(geoJson, sizeof(geoJson), "{\"type\":\"%s\",\"coordinates\":%s}", geometryP->value.s, coordsBuf);

  if ((len <= 0) || (len >= (int) sizeof(geoJson)))
    KT_RVE("Sub '%s': 'geoQ' coordinates too big for the GEOS reader (%d bytes)", sciP->subId, len);

  GEOSGeoJSONReader* reader = GEOSGeoJSONReader_create_r(geosHandle);

  sciP->geosGeometry = GEOSGeoJSONReader_readGeometry_r(geosHandle, reader, geoJson);
  GEOSGeoJSONReader_destroy_r(geosHandle, reader);

  if ((sciP->geosGeometry != NULL) && (sciP->geoInfo->georel != GeorelNear))
    sciP->geosPrepared = GEOSPrepare_r(geosHandle, sciP->geosGeometry);

  KT_T(KtSubCache, "Sub '%s': geoQ compiled (geosGeometry: %p, geosPrepared: %p)", sciP->subId, sciP->geosGeometry, sciP->geosPrepared);
}
