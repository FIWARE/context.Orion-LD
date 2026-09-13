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
#include "kjson/KjNode.h"                                    // KjNode
}

#include "orionld/types/OrionldGeoInfo.h"                    // OrionldGeoInfo



// -----------------------------------------------------------------------------
//
// geoMatch - does the Entity match the geo-filter?
//
// 'geosGeometry' and 'geosPrepared' are the compiled form of 'geoInfoP' (see geoCompile), and
// 'what' names the owner of the filter - a subscription id, or "geoQ" for a query - for the
// trace and error messages.
//
// Returns true if the Entity matches, and also if there is no geo-filter at all.
//
extern bool geoMatch
(
  OrionldGeoInfo*               geoInfoP,
  GEOSGeometry*                 geosGeometry,
  const GEOSPreparedGeometry*   geosPrepared,
  const char*                   what,
  KjNode*                       apiEntityP
);
