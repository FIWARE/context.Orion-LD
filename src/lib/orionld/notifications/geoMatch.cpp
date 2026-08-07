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
#include <math.h>                                              // sin, cos, asin, sqrt, M_PI
#include <geos_c.h>

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjRenderSize.h"                                // kjFastRenderSize
#include "kjson/kjRender.h"                                    // kjFastRender
#include "kalloc/kaAlloc.h"                                    // kaAlloc
}

#include "orionld/types/OrionldGeoInfo.h"                      // OrionldGeoInfo
#include "orionld/types/OrionldGeorel.h"                       // OrionldGeorel
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/geosInit.h"                           // geosHandle
#include "orionld/types/SubCacheItem.h"                        // SubCacheItem
#include "orionld/notifications/geoMatch.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// EARTH_RADIUS_M -
//
#define EARTH_RADIUS_M  6371000.0
#define DEG_TO_RAD      (M_PI / 180.0)



// -----------------------------------------------------------------------------
//
// haversineDistance - distance in meters between two (lon, lat) points
//
static double haversineDistance(double lon1, double lat1, double lon2, double lat2)
{
  double dLat = (lat2 - lat1) * DEG_TO_RAD;
  double dLon = (lon2 - lon1) * DEG_TO_RAD;

  lat1 *= DEG_TO_RAD;
  lat2 *= DEG_TO_RAD;

  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);

  return EARTH_RADIUS_M * 2 * asin(sqrt(a));
}



// -----------------------------------------------------------------------------
//
// entityGeoJsonGet - extract the GeoJSON value from an entity's geoproperty
//
// The entity attribute looks like:
//   "location": { "type": "GeoProperty", "value": { "type": "Point", "coordinates": [lon, lat] } }
//
// Returns the GeoJSON node (the "value" child), or NULL if not found.
//
static KjNode* entityGeoJsonGet(KjNode* entityP, const char* geoProperty)
{
  const char* attrName = (geoProperty != NULL) ? geoProperty : "location";
  KjNode*     attrP    = kjLookup(entityP, attrName);

  if (attrP == NULL)
    return NULL;

  // The GeoJSON is in the "value" field of the GeoProperty
  KjNode* valueP = kjLookup(attrP, "value");

  return valueP;
}



// -----------------------------------------------------------------------------
//
// entityCoordsGet - extract lon/lat from entity's geoproperty Point for 'near'
//
// Returns true if a valid Point was found
//
static bool entityCoordsGet(KjNode* entityP, const char* geoProperty, double* lonP, double* latP)
{
  KjNode* geoJsonP = entityGeoJsonGet(entityP, geoProperty);
  if (geoJsonP == NULL)
  {
    KT_T(KtSubCacheMatch, "entityCoordsGet: geoProperty '%s' not found in entity", geoProperty ? geoProperty : "location");
    return false;
  }

  KjNode* typeP = kjLookup(geoJsonP, "type");
  if (typeP == NULL || typeP->type != KjString || strcmp(typeP->value.s, "Point") != 0)
    return false;

  KjNode* coordsP = kjLookup(geoJsonP, "coordinates");
  if (coordsP == NULL || coordsP->type != KjArray)
    return false;

  KjNode* lonNode = coordsP->value.firstChildP;
  if (lonNode == NULL)
    return false;

  KjNode* latNode = lonNode->next;
  if (latNode == NULL)
    return false;

  *lonP = (lonNode->type == KjFloat) ? lonNode->value.f : (double) lonNode->value.i;
  *latP = (latNode->type == KjFloat) ? latNode->value.f : (double) latNode->value.i;

  return true;
}



// -----------------------------------------------------------------------------
//
// subCoordsGet - extract lon/lat from subscription's reference Point coordinates
//
static bool subCoordsGet(KjNode* coordsP, double* lonP, double* latP)
{
  if (coordsP == NULL || coordsP->type != KjArray)
    return false;

  KjNode* lonNode = coordsP->value.firstChildP;
  if (lonNode == NULL)
    return false;

  KjNode* latNode = lonNode->next;
  if (latNode == NULL)
    return false;

  *lonP = (lonNode->type == KjFloat) ? lonNode->value.f : (double) lonNode->value.i;
  *latP = (latNode->type == KjFloat) ? latNode->value.f : (double) latNode->value.i;

  return true;
}



// -----------------------------------------------------------------------------
//
// nearMatch - handle the 'near' georel using haversine distance
//
static bool nearMatch(SubCacheItem* sciP, KjNode* entityP)
{
  double entityLon, entityLat;
  double subLon, subLat;

  if (entityCoordsGet(entityP, sciP->geoInfo->geoProperty, &entityLon, &entityLat) == false)
  {
    KT_T(KtSubCacheMatch, "nearMatch: entityCoordsGet failed");
    return false;
  }

  if (subCoordsGet(sciP->geoInfo->coordinates, &subLon, &subLat) == false)
  {
    KT_T(KtSubCacheMatch, "nearMatch: subCoordsGet failed (coords type=%d)", sciP->geoInfo->coordinates ? sciP->geoInfo->coordinates->type : -1);
    return false;
  }

  double distance = haversineDistance(entityLon, entityLat, subLon, subLat);

  KT_T(KtSubCacheMatch, "near: distance=%f, maxDistance=%d, minDistance=%d",
       distance, sciP->geoInfo->maxDistance, sciP->geoInfo->minDistance);

  if (sciP->geoInfo->maxDistance > 0 && distance > sciP->geoInfo->maxDistance)
    return false;

  if (sciP->geoInfo->minDistance > 0 && distance < sciP->geoInfo->minDistance)
    return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// geosPredicateMatch - handle topological georel predicates using GEOS
//
static bool geosPredicateMatch(SubCacheItem* sciP, KjNode* entityP)
{
  KjNode* geoJsonP = entityGeoJsonGet(entityP, sciP->geoInfo->geoProperty);
  if (geoJsonP == NULL)
    return false;

  // Render the entity's GeoJSON to a string
  int   bufSize = kjFastRenderSize(geoJsonP);
  char* buf     = kaAlloc(&orionldState.kalloc, bufSize);
  kjFastRender(geoJsonP, buf);

  // Parse entity geometry with GEOS
  GEOSGeoJSONReader* reader      = GEOSGeoJSONReader_create_r(geosHandle);
  GEOSGeometry*      entityGeom  = GEOSGeoJSONReader_readGeometry_r(geosHandle, reader, buf);
  GEOSGeoJSONReader_destroy_r(geosHandle, reader);

  if (entityGeom == NULL)
  {
    KT_W("Failed to parse entity GeoJSON: %s", buf);
    return false;
  }

  char result = 0;

  switch (sciP->geoInfo->georel)
  {
  case GeorelWithin:
    // "within" = entity is within subscription's reference geometry
    // PreparedContains(subGeom, entityGeom) == true means the sub polygon contains the entity
    result = GEOSPreparedContains_r(geosHandle, sciP->geosPrepared, entityGeom);
    break;

  case GeorelContains:
    // "contains" = entity's geometry contains the subscription's reference geometry
    // PreparedWithin(subGeom, entityGeom) == true means the sub geometry is within the entity
    result = GEOSPreparedWithin_r(geosHandle, sciP->geosPrepared, entityGeom);
    break;

  case GeorelIntersects:
    result = GEOSPreparedIntersects_r(geosHandle, sciP->geosPrepared, entityGeom);
    break;

  case GeorelEquals:
    result = GEOSEquals_r(geosHandle, sciP->geosGeometry, entityGeom);
    break;

  case GeorelDisjoint:
    result = GEOSPreparedDisjoint_r(geosHandle, sciP->geosPrepared, entityGeom);
    break;

  case GeorelOverlaps:
    result = GEOSPreparedOverlaps_r(geosHandle, sciP->geosPrepared, entityGeom);
    break;

  default:
    KT_W("Unexpected georel %d in geosPredicateMatch", sciP->geoInfo->georel);
    break;
  }

  GEOSGeom_destroy_r(geosHandle, entityGeom);

  return (result == 1);
}



// -----------------------------------------------------------------------------
//
// geoMatch - check if an entity matches a subscription's geoQ filter
//
// Returns true if the entity matches (or if the subscription has no geoQ)
//
bool geoMatch(SubCacheItem* sciP, KjNode* finalApiEntityP)
{
  if (sciP->geoInfo == NULL)
  {
    KT_T(KtSubCacheMatch, "Sub '%s': no geoInfo - geoMatch returns true", sciP->subId);
    return true;
  }

  KT_T(KtSubCacheMatch, "Sub '%s': geoMatch - georel=%d, geoProperty='%s'",
       sciP->subId, sciP->geoInfo->georel, sciP->geoInfo->geoProperty ? sciP->geoInfo->geoProperty : "NULL");

  // Trace entity attribute names to understand the format
  if (finalApiEntityP != NULL)
  {
    for (KjNode* attrP = finalApiEntityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      KT_T(KtSubCacheMatch, "  entity attr: '%s'", attrP->name);
  }

  if (sciP->geoInfo->georel == GeorelNear)
  {
    bool r = nearMatch(sciP, finalApiEntityP);
    KT_T(KtSubCacheMatch, "Sub '%s': nearMatch returns %s", sciP->subId, r ? "true" : "false");
    return r;
  }

  if (sciP->geosPrepared == NULL && sciP->geoInfo->georel != GeorelEquals)
  {
    KT_W("No prepared GEOS geometry for subscription %s", sciP->subId);
    return false;
  }

  bool r = geosPredicateMatch(sciP, finalApiEntityP);
  KT_T(KtSubCacheMatch, "Sub '%s': geosPredicateMatch returns %s", sciP->subId, r ? "true" : "false");
  return r;
}
