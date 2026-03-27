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
#include <string.h>                                              // strcmp, strlen
#include <stdlib.h>                                              // atoll

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
}

#include "orionld/types/PgConnection.h"                          // PgConnection
#include "orionld/types/StringArray.h"                           // StringArray
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/pqHeader.h"                             // PGresult, PQexecParams, etc.
#include "orionld/troe/pgConnectionGet.h"                        // pgConnectionGet
#include "orionld/troe/pgConnectionRelease.h"                    // pgConnectionRelease
#include "orionld/troe/pgTemporalEntitiesQuery.h"                // Own interface



// -----------------------------------------------------------------------------
//
// typeFilter - build an SQL IN clause for entity type filtering
//
static const char* typeFilter(StringArray* typeList)
{
  if (typeList == NULL || typeList->items == 0)
    return "";

  int needed = 20;  // " AND type IN (" + ")"
  for (int i = 0; i < typeList->items; i++)
    needed += strlen(typeList->array[i]) + 3;

  char* buf = (char*) kaAlloc(&orionldState.kalloc, needed);
  int   pos = 0;

  pos += snprintf(buf + pos, needed - pos, " AND type IN (");
  for (int i = 0; i < typeList->items; i++)
  {
    if (i > 0) buf[pos++] = ',';
    pos += snprintf(buf + pos, needed - pos, "'%s'", typeList->array[i]);
  }
  buf[pos++] = ')';
  buf[pos]   = 0;

  return buf;
}



// -----------------------------------------------------------------------------
//
// idFilter - build an SQL IN clause for entity id filtering
//
static const char* idFilter(StringArray* idList)
{
  if (idList == NULL || idList->items == 0)
    return "";

  int needed = 18;  // " AND id IN (" + ")"
  for (int i = 0; i < idList->items; i++)
    needed += strlen(idList->array[i]) + 3;

  char* buf = (char*) kaAlloc(&orionldState.kalloc, needed);
  int   pos = 0;

  pos += snprintf(buf + pos, needed - pos, " AND id IN (");
  for (int i = 0; i < idList->items; i++)
  {
    if (i > 0) buf[pos++] = ',';
    pos += snprintf(buf + pos, needed - pos, "'%s'", idList->array[i]);
  }
  buf[pos++] = ')';
  buf[pos]   = 0;

  return buf;
}



// -----------------------------------------------------------------------------
//
// pgTemporalEntitiesQuery -
//
// Query the entities table for distinct entity IDs matching type/id/time filters.
// Supports pagination (limit/offset) and optional count.
//
// The result set contains rows of (id, type) for each matched entity.
//
bool pgTemporalEntitiesQuery
(
  StringArray*  typeList,
  StringArray*  idList,
  const char*   idPattern,
  const char*   qFilter,
  const char*   geoFilter,
  int           limit,
  int           offset,
  long long*    countP,
  PGresult**    entityResP
)
{
  *entityResP = NULL;

  PgConnection* connectionP = pgConnectionGet(orionldState.tenantP->troeDbName);
  if ((connectionP == NULL) || (connectionP->connectionP == NULL))
  {
    KT_E("pgTemporalEntitiesQuery: no connection to postgres");
    return false;
  }

  // Build filter fragments
  const char* typeF = typeFilter(typeList);
  const char* idF   = idFilter(idList);

  // idPattern uses a parameterized value for safety
  const char* idPatternClause = "";

  // Entity discovery does NOT filter by time on the entities table.
  // Time filtering is done at the attribute level in pgTemporalEntityQuery,
  // using the correct timeproperty column (observedAt, ts, etc.).
  // The entities table `ts` is the record-creation timestamp, which may differ
  // significantly from observedAt (e.g., inserting historical data).

  char idPatternBuf[64] = "";
  if (idPattern != NULL)
  {
    snprintf(idPatternBuf, sizeof(idPatternBuf), " AND id ~ $1");
    idPatternClause = idPatternBuf;
  }

  int totalParams = (idPattern != NULL ? 1 : 0);

  // Build qFilter clause (may be empty or " AND <exists subqueries>")
  char qFilterClause[4096] = "";
  if (qFilter != NULL && qFilter[0] != 0)
    snprintf(qFilterClause, sizeof(qFilterClause), " AND %s", qFilter);

  // Build geoFilter clause (may be empty or " AND EXISTS(...)")
  char geoFilterClause[4096] = "";
  if (geoFilter != NULL && geoFilter[0] != 0)
    snprintf(geoFilterClause, sizeof(geoFilterClause), " AND %s", geoFilter);

  // Build the main query
  char query[16384];
  snprintf(query, sizeof(query),
           "SELECT id, type FROM ("
           "SELECT DISTINCT ON (id) id, type FROM entities "
           "WHERE 1=1%s%s%s%s%s "
           "ORDER BY id, ts DESC"
           ") sub ORDER BY id LIMIT %d OFFSET %d",
           typeF, idF, idPatternClause, qFilterClause, geoFilterClause, limit, offset);

  // Build param values array (NULL when no params to avoid undefined pointer)
  const char*  paramValues[4] = { NULL, NULL, NULL, NULL };
  const char** paramValuesP   = NULL;
  int paramIdx = 0;

  if (idPattern != NULL)
  {
    paramValues[paramIdx++] = idPattern;
    paramValuesP = paramValues;
  }

  KT_T(KtSql, "SQL[entities]: %s (totalParams=%d)", query, totalParams);

  // Run count query if requested
  if (countP != NULL)
  {
    char countQuery[16384];
    snprintf(countQuery, sizeof(countQuery),
             "SELECT COUNT(*) FROM ("
             "SELECT DISTINCT ON (id) id FROM entities "
             "WHERE 1=1%s%s%s%s%s "
             "ORDER BY id, ts DESC"
             ") sub",
             typeF, idF, idPatternClause, qFilterClause, geoFilterClause);

    PGresult* countRes = PQexecParams(connectionP->connectionP, countQuery,
                                      totalParams, NULL, paramValuesP, NULL, NULL, 0);

    if (PQresultStatus(countRes) != PGRES_TUPLES_OK)
    {
      KT_E("pgTemporalEntitiesQuery: count query failed: %s", PQresultErrorMessage(countRes));
      PQclear(countRes);
      pgConnectionRelease(connectionP);
      return false;
    }

    if (PQntuples(countRes) > 0)
      *countP = atoll(PQgetvalue(countRes, 0, 0));
    else
      *countP = 0;

    PQclear(countRes);
  }

  // Run the main entity query
  *entityResP = PQexecParams(connectionP->connectionP, query,
                             totalParams, NULL, paramValuesP, NULL, NULL, 0);

  if (PQresultStatus(*entityResP) != PGRES_TUPLES_OK)
  {
    KT_E("pgTemporalEntitiesQuery: entity query failed: %s", PQresultErrorMessage(*entityResP));
    PQclear(*entityResP);
    *entityResP = NULL;
    pgConnectionRelease(connectionP);
    return false;
  }

  pgConnectionRelease(connectionP);
  return true;
}
