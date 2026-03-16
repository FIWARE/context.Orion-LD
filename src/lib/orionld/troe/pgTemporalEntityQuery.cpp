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
*/
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
// SQL query templates
//
// Entity query: get the entity type at a given point in time
//
static const char* entityQueryBefore =
  "SELECT id, type FROM entities "
  "WHERE id = $1 AND ts <= $2 "
  "ORDER BY ts DESC LIMIT 1";

static const char* entityQueryAfter =
  "SELECT id, type FROM entities "
  "WHERE id = $1 AND ts >= $2 "
  "ORDER BY ts ASC LIMIT 1";

static const char* entityQueryBetween =
  "SELECT id, type FROM entities "
  "WHERE id = $1 AND ts >= $2 AND ts <= $3 "
  "ORDER BY ts DESC LIMIT 1";

//
// Attribute query: get latest attribute values at a given point in time
//
static const char* attrQueryBefore =
  "SELECT DISTINCT ON (id, \"datasetId\") "
  "id, \"valueType\"::text, text, boolean, number, datetime, compound, "
  "\"observedAt\", \"unitCode\", \"datasetId\", \"subProperties\", "
  "ST_AsGeoJSON(\"geoPoint\") as \"geoPoint\", "
  "ST_AsGeoJSON(\"geoPolygon\") as \"geoPolygon\", "
  "ST_AsGeoJSON(\"geoMultiPoint\") as \"geoMultiPoint\", "
  "ST_AsGeoJSON(\"geoMultiPolygon\") as \"geoMultiPolygon\", "
  "ST_AsGeoJSON(\"geoLineString\") as \"geoLineString\", "
  "ST_AsGeoJSON(\"geoMultiLineString\") as \"geoMultiLineString\" "
  "FROM attributes "
  "WHERE \"entityId\" = $1 AND ts <= $2 AND \"opMode\" != 'Delete' "
  "ORDER BY id, \"datasetId\", ts DESC";

static const char* attrQueryAfter =
  "SELECT DISTINCT ON (id, \"datasetId\") "
  "id, \"valueType\"::text, text, boolean, number, datetime, compound, "
  "\"observedAt\", \"unitCode\", \"datasetId\", \"subProperties\", "
  "ST_AsGeoJSON(\"geoPoint\") as \"geoPoint\", "
  "ST_AsGeoJSON(\"geoPolygon\") as \"geoPolygon\", "
  "ST_AsGeoJSON(\"geoMultiPoint\") as \"geoMultiPoint\", "
  "ST_AsGeoJSON(\"geoMultiPolygon\") as \"geoMultiPolygon\", "
  "ST_AsGeoJSON(\"geoLineString\") as \"geoLineString\", "
  "ST_AsGeoJSON(\"geoMultiLineString\") as \"geoMultiLineString\" "
  "FROM attributes "
  "WHERE \"entityId\" = $1 AND ts >= $2 AND \"opMode\" != 'Delete' "
  "ORDER BY id, \"datasetId\", ts ASC";

static const char* attrQueryBetween =
  "SELECT DISTINCT ON (id, \"datasetId\") "
  "id, \"valueType\"::text, text, boolean, number, datetime, compound, "
  "\"observedAt\", \"unitCode\", \"datasetId\", \"subProperties\", "
  "ST_AsGeoJSON(\"geoPoint\") as \"geoPoint\", "
  "ST_AsGeoJSON(\"geoPolygon\") as \"geoPolygon\", "
  "ST_AsGeoJSON(\"geoMultiPoint\") as \"geoMultiPoint\", "
  "ST_AsGeoJSON(\"geoMultiPolygon\") as \"geoMultiPolygon\", "
  "ST_AsGeoJSON(\"geoLineString\") as \"geoLineString\", "
  "ST_AsGeoJSON(\"geoMultiLineString\") as \"geoMultiLineString\" "
  "FROM attributes "
  "WHERE \"entityId\" = $1 AND ts >= $2 AND ts <= $3 AND \"opMode\" != 'Delete' "
  "ORDER BY id, \"datasetId\", ts DESC";

//
// Sub-attribute query
//
static const char* subAttrQueryBefore =
  "SELECT DISTINCT ON (id, \"attrInstanceId\", \"attrDatasetId\") "
  "id, \"attrInstanceId\", \"attrDatasetId\", \"valueType\"::text, "
  "text, boolean, number, datetime, compound, "
  "\"observedAt\", \"unitCode\", "
  "ST_AsGeoJSON(\"geoPoint\") as \"geoPoint\" "
  "FROM \"subAttributes\" "
  "WHERE \"entityId\" = $1 AND ts <= $2 "
  "ORDER BY id, \"attrInstanceId\", \"attrDatasetId\", ts DESC";

static const char* subAttrQueryAfter =
  "SELECT DISTINCT ON (id, \"attrInstanceId\", \"attrDatasetId\") "
  "id, \"attrInstanceId\", \"attrDatasetId\", \"valueType\"::text, "
  "text, boolean, number, datetime, compound, "
  "\"observedAt\", \"unitCode\", "
  "ST_AsGeoJSON(\"geoPoint\") as \"geoPoint\" "
  "FROM \"subAttributes\" "
  "WHERE \"entityId\" = $1 AND ts >= $2 "
  "ORDER BY id, \"attrInstanceId\", \"attrDatasetId\", ts ASC";

static const char* subAttrQueryBetween =
  "SELECT DISTINCT ON (id, \"attrInstanceId\", \"attrDatasetId\") "
  "id, \"attrInstanceId\", \"attrDatasetId\", \"valueType\"::text, "
  "text, boolean, number, datetime, compound, "
  "\"observedAt\", \"unitCode\", "
  "ST_AsGeoJSON(\"geoPoint\") as \"geoPoint\" "
  "FROM \"subAttributes\" "
  "WHERE \"entityId\" = $1 AND ts >= $2 AND ts <= $3 "
  "ORDER BY id, \"attrInstanceId\", \"attrDatasetId\", ts DESC";



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

  // Select query variants based on timerel
  const char*  entityQuery;
  const char*  attrQuery;
  const char*  subAttrQuery;
  int          nParams;

  if (strcmp(timerel, "before") == 0)
  {
    entityQuery  = entityQueryBefore;
    attrQuery    = attrQueryBefore;
    subAttrQuery = subAttrQueryBefore;
    nParams      = 2;
  }
  else if (strcmp(timerel, "after") == 0)
  {
    entityQuery  = entityQueryAfter;
    attrQuery    = attrQueryAfter;
    subAttrQuery = subAttrQueryAfter;
    nParams      = 2;
  }
  else if (strcmp(timerel, "between") == 0)
  {
    entityQuery  = entityQueryBetween;
    attrQuery    = attrQueryBetween;
    subAttrQuery = subAttrQueryBetween;
    nParams      = 3;
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
  // Query 2: Attributes
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
