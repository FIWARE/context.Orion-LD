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
* Author: Carsten Frey
*/
#include <stdio.h>                                            // snprintf
#include <string.h>                                            // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/types/PgConnection.h"                        // PgConnection
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/pqHeader.h"                           // PGresult, PQexecParams, etc.
#include "orionld/troe/pgConnectionGet.h"                      // pgConnectionGet
#include "orionld/troe/pgConnectionRelease.h"                  // pgConnectionRelease
#include "orionld/troe/pgTemporalEntityQuery.h"                // Own interface



// -----------------------------------------------------------------------------
//
// timeColumnForTimeproperty - determine the SQL time column from the timeproperty URI param
//
// Per ETSI GS CIM 009 (Clause 4.5.6):
//   - observedAt (default): filter on the observedat column
//   - modifiedAt: filter on ts (the record timestamp)
//   - createdAt:  filter on ts with additional opmode='Create' constraint
//
static const char* timeColumnForTimeproperty(const char* timeproperty)
{
  if (timeproperty == NULL)
    return "observedat";

  if (strcmp(timeproperty, "modifiedAt") == 0)
    return "ts";

  if (strcmp(timeproperty, "createdAt") == 0)
    return "ts";

  return "observedat";
}



// -----------------------------------------------------------------------------
//
// SQL query SELECT/FROM/ORDER parts (shared between all timerel variants)
//
static const char* entitySelect =
  "SELECT id, type FROM entities ";

static const char* attrSelect =
  "SELECT "
  "id, valuetype::text, text, boolean, number, datetime, compound, "
  "observedat, unitcode, datasetid, subproperties, "
  "ST_AsGeoJSON(geopoint) as geopoint, "
  "ST_AsGeoJSON(geopolygon) as geopolygon, "
  "ST_AsGeoJSON(geomultipoint) as geomultipoint, "
  "ST_AsGeoJSON(geomultipolygon) as geomultipolygon, "
  "ST_AsGeoJSON(geolinestring) as geolinestring, "
  "ST_AsGeoJSON(geomultilinestring) as geomultilinestring, "
  "instanceid "
  "FROM attributes ";

static const char* subAttrSelect =
  "SELECT "
  "id, attrinstanceid, attrdatasetid, valuetype::text, "
  "text, boolean, number, datetime, compound, "
  "observedat, unitcode, "
  "ST_AsGeoJSON(geopoint) as geopoint "
  "FROM subattributes ";



// -----------------------------------------------------------------------------
//
// pgTemporalEntityQuery -
//
bool pgTemporalEntityQuery
(
  const char*  entityId,
  const char*  timerel,
  const char*  timeAt,
  const char*  endTimeAt,
  const char*  timeproperty,
  PGresult**   entityResP,
  PGresult**   attrResP,
  PGresult**   subAttrResP
)
{
  *entityResP  = NULL;
  *attrResP    = NULL;
  *subAttrResP = NULL;

  PgConnection* connectionP = pgConnectionGet(orionldState.tenantP->troeDbName);
  if ((connectionP == NULL) || (connectionP->connectionP == NULL))
  {
    KT_E("pgTemporalEntityQuery: no connection to postgres");
    return false;
  }

  const char* timeCol = timeColumnForTimeproperty(timeproperty);
  bool createdAtFilter = (timeproperty != NULL && strcmp(timeproperty, "createdAt") == 0);

  //
  // Build query strings dynamically based on timerel and timeproperty
  //
  char entityQuery[512];
  char attrQuery[2048];
  char subAttrQuery[1024];
  int  nParams;

  const char* opmodeFilter = createdAtFilter ? " AND opmode = 'Create'" : " AND opmode != 'Delete'";

  if (strcmp(timerel, "before") == 0)
  {
    snprintf(entityQuery, sizeof(entityQuery),
             "%sWHERE id = $1 AND ts <= $2 ORDER BY ts DESC LIMIT 1", entitySelect);
    snprintf(attrQuery, sizeof(attrQuery),
             "%sWHERE entityid = $1 AND %s <= $2%s ORDER BY id, datasetid, ts DESC",
             attrSelect, timeCol, opmodeFilter);
    snprintf(subAttrQuery, sizeof(subAttrQuery),
             "%sWHERE entityid = $1 AND ts <= $2 ORDER BY id, attrinstanceid, attrdatasetid, ts DESC",
             subAttrSelect);
    nParams = 2;
  }
  else if (strcmp(timerel, "after") == 0)
  {
    snprintf(entityQuery, sizeof(entityQuery),
             "%sWHERE id = $1 AND ts >= $2 ORDER BY ts ASC LIMIT 1", entitySelect);
    snprintf(attrQuery, sizeof(attrQuery),
             "%sWHERE entityid = $1 AND %s >= $2%s ORDER BY id, datasetid, ts ASC",
             attrSelect, timeCol, opmodeFilter);
    snprintf(subAttrQuery, sizeof(subAttrQuery),
             "%sWHERE entityid = $1 AND ts >= $2 ORDER BY id, attrinstanceid, attrdatasetid, ts ASC",
             subAttrSelect);
    nParams = 2;
  }
  else if (strcmp(timerel, "between") == 0)
  {
    snprintf(entityQuery, sizeof(entityQuery),
             "%sWHERE id = $1 AND ts >= $2 AND ts <= $3 ORDER BY ts DESC LIMIT 1", entitySelect);
    snprintf(attrQuery, sizeof(attrQuery),
             "%sWHERE entityid = $1 AND %s >= $2 AND %s <= $3%s ORDER BY id, datasetid, ts DESC",
             attrSelect, timeCol, timeCol, opmodeFilter);
    snprintf(subAttrQuery, sizeof(subAttrQuery),
             "%sWHERE entityid = $1 AND ts >= $2 AND ts <= $3 ORDER BY id, attrinstanceid, attrdatasetid, ts DESC",
             subAttrSelect);
    nParams = 3;
  }
  else
  {
    KT_E("pgTemporalEntityQuery: invalid timerel '%s'", timerel);
    pgConnectionRelease(connectionP);
    return false;
  }

  // Build parameter arrays
  const char* params2[] = { entityId, timeAt };
  const char* params3[] = { entityId, timeAt, endTimeAt };
  const char** paramValues = (nParams == 3) ? params3 : params2;

  //
  // Query 1: Entity type
  //
  KT_T(KtSql, "SQL[entity]: %s (entityId=%s, timeAt=%s)", entityQuery, entityId, timeAt);
  *entityResP = PQexecParams(connectionP->connectionP, entityQuery, nParams, NULL, paramValues, NULL, NULL, 0);
  if (*entityResP == NULL || PQresultStatus(*entityResP) != PGRES_TUPLES_OK)
  {
    KT_E("pgTemporalEntityQuery: entity query failed: %s", PQerrorMessage(connectionP->connectionP));
    if (*entityResP != NULL)
      PQclear(*entityResP);
    *entityResP = NULL;
    pgConnectionRelease(connectionP);
    return false;
  }

  // If no entity found, return successfully but with empty result (caller checks ntuples)
  if (PQntuples(*entityResP) == 0)
  {
    pgConnectionRelease(connectionP);
    return true;
  }

  //
  // Query 2: Attributes (all instances in the time window)
  //
  KT_T(KtSql, "SQL[attrs]: %s", attrQuery);
  *attrResP = PQexecParams(connectionP->connectionP, attrQuery, nParams, NULL, paramValues, NULL, NULL, 0);
  if (*attrResP == NULL || PQresultStatus(*attrResP) != PGRES_TUPLES_OK)
  {
    KT_E("pgTemporalEntityQuery: attributes query failed: %s", PQerrorMessage(connectionP->connectionP));
    if (*attrResP != NULL)
      PQclear(*attrResP);
    *attrResP = NULL;
    pgConnectionRelease(connectionP);
    return false;
  }

  //
  // Query 3: Sub-Attributes
  //
  KT_T(KtSql, "SQL[subAttrs]: %s", subAttrQuery);
  *subAttrResP = PQexecParams(connectionP->connectionP, subAttrQuery, nParams, NULL, paramValues, NULL, NULL, 0);
  if (*subAttrResP == NULL || PQresultStatus(*subAttrResP) != PGRES_TUPLES_OK)
  {
    KT_E("pgTemporalEntityQuery: sub-attributes query failed: %s", PQerrorMessage(connectionP->connectionP));
    if (*subAttrResP != NULL)
      PQclear(*subAttrResP);
    *subAttrResP = NULL;
    pgConnectionRelease(connectionP);
    return false;
  }

  pgConnectionRelease(connectionP);
  return true;
}
