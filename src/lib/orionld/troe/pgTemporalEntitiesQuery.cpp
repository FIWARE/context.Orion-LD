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
  const char*   timerel,
  const char*   timeAt,
  const char*   endTimeAt,
  const char*   qFilter,
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
  int         idPatternParam  = 0;

  // Determine number of time params and build the time WHERE clause
  int         nTimeParams = 0;
  const char* timeClause  = "";

  if (strcmp(timerel, "before") == 0)
  {
    nTimeParams = 1;
    timeClause  = " AND ts <= $1";
  }
  else if (strcmp(timerel, "after") == 0)
  {
    nTimeParams = 1;
    timeClause  = " AND ts >= $1";
  }
  else if (strcmp(timerel, "between") == 0)
  {
    nTimeParams = 2;
    timeClause  = " AND ts >= $1 AND ts <= $2";
  }

  // idPattern gets next param number after time params
  char idPatternBuf[64] = "";
  if (idPattern != NULL)
  {
    idPatternParam = nTimeParams + 1;
    snprintf(idPatternBuf, sizeof(idPatternBuf), " AND id ~ $%d", idPatternParam);
    idPatternClause = idPatternBuf;
  }

  int totalParams = nTimeParams + (idPattern != NULL ? 1 : 0);

  // Build qFilter clause (may be empty or " AND <exists subqueries>")
  char qFilterClause[4096] = "";
  if (qFilter != NULL && qFilter[0] != 0)
    snprintf(qFilterClause, sizeof(qFilterClause), " AND %s", qFilter);

  // Build the main query
  char query[8192];
  snprintf(query, sizeof(query),
           "SELECT id, type FROM ("
           "SELECT DISTINCT ON (id) id, type FROM entities "
           "WHERE 1=1%s%s%s%s%s "
           "ORDER BY id, ts DESC"
           ") sub ORDER BY id LIMIT %d OFFSET %d",
           timeClause, typeF, idF, idPatternClause, qFilterClause, limit, offset);

  // Build param values array
  const char* paramValues[4];
  int paramIdx = 0;

  if (nTimeParams >= 1)
    paramValues[paramIdx++] = timeAt;
  if (nTimeParams >= 2)
    paramValues[paramIdx++] = endTimeAt;
  if (idPattern != NULL)
    paramValues[paramIdx++] = idPattern;

  // Run count query if requested
  if (countP != NULL)
  {
    char countQuery[8192];
    snprintf(countQuery, sizeof(countQuery),
             "SELECT COUNT(*) FROM ("
             "SELECT DISTINCT ON (id) id FROM entities "
             "WHERE 1=1%s%s%s%s%s "
             "ORDER BY id, ts DESC"
             ") sub",
             timeClause, typeF, idF, idPatternClause, qFilterClause);

    PGresult* countRes = PQexecParams(connectionP->connectionP, countQuery,
                                      totalParams, NULL, paramValues, NULL, NULL, 0);

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
                             totalParams, NULL, paramValues, NULL, NULL, 0);

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
