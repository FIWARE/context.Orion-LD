#ifndef SRC_LIB_ORIONLD_TROE_GEOFILTERTOSQL_H_
#define SRC_LIB_ORIONLD_TROE_GEOFILTERTOSQL_H_

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
#include "orionld/types/OrionldGeoInfo.h"                       // OrionldGeoInfo



// -----------------------------------------------------------------------------
//
// geoFilterToSql - convert an OrionldGeoInfo to a SQL WHERE clause fragment
//
// Generates an EXISTS subquery that filters entities based on their geo-property
// using PostGIS spatial functions (ST_DWithin, ST_Intersects, ST_Contains, etc.)
//
// Returns the SQL fragment string, or NULL if geoInfoP is NULL or on error.
//
extern const char* geoFilterToSql(OrionldGeoInfo* geoInfoP);

#endif  // SRC_LIB_ORIONLD_TROE_GEOFILTERTOSQL_H_
