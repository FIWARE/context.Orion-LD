/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/types/PgConnection.h"                        // PgConnection
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/pqHeader.h"                           // Postgres header
#include "orionld/troe/pgConnectionGet.h"                      // pgConnectionGet
#include "orionld/troe/pgConnectionRelease.h"                  // pgConnectionRelease
#include "orionld/troe/pgTransactionBegin.h"                   // pgTransactionBegin
#include "orionld/troe/pgTransactionRollback.h"                // pgTransactionRollback
#include "orionld/troe/pgTransactionCommit.h"                  // pgTransactionCommit
#include "orionld/troe/pgCommands.h"                           // Own interface

#include <stdio.h>                                             // snprintf
#include <string.h>                                            // strlen



// -----------------------------------------------------------------------------
//
// troeErrorStringSet - stash the underlying Postgres error text (trimmed) so the Kafka NACK can carry it
//
static void troeErrorStringSet(const char* text)
{
  if ((text == NULL) || (text[0] == 0))
    return;

  snprintf(orionldState.troeErrorString, sizeof(orionldState.troeErrorString), "%s", text);

  // libpq error strings end in a newline; trim trailing whitespace so the NACK reads cleanly
  int last = (int) strlen(orionldState.troeErrorString) - 1;
  while ((last >= 0) && ((orionldState.troeErrorString[last] == '\n') || (orionldState.troeErrorString[last] == '\r') || (orionldState.troeErrorString[last] == ' ')))
    orionldState.troeErrorString[last--] = 0;
}



// -----------------------------------------------------------------------------
//
// pgConnectionInvalidate - drop a dead PGconn so the pool reconnects fresh next time
//
// A *connection* failure (PQexec returned NULL, or PQstatus != CONNECTION_OK) leaves a dead PGconn in
// the pool that PQstatus may still report as CONNECTION_OK on the next borrow - so it is handed out
// again and fails again. Finish it and NULL the slot's connection here; the next pgConnectionGet then
// reconnects via pgConnect's retry loop. Without this a transient Postgres outage becomes a PERMANENT
// stall (the redelivered Kafka batch keeps hitting the same dead connection) until the broker restarts.
//
// NOT called for a rejected SQL statement (constraint violation, aborted transaction, missing column):
// there the connection is still healthy and must be kept.
//
static void pgConnectionInvalidate(PgConnection* connectionP)
{
  if (connectionP->connectionP != NULL)
  {
    PQfinish(connectionP->connectionP);
    connectionP->connectionP = NULL;
  }
}



// -----------------------------------------------------------------------------
//
// pgCommands -
//
void pgCommands(char* sql[], int commands)
{
  PgConnection* connectionP = pgConnectionGet(orionldState.tenantP->troeDbName);

  if ((connectionP == NULL) || (connectionP->connectionP == NULL))
  {
    orionldState.troeError = true;  // make the TRoE write failure observable to the caller
    troeErrorStringSet("no connection to Postgres");
    KT_RVE("no connection to postgres");
  }

  if (pgTransactionBegin(connectionP->connectionP) != true)
  {
    orionldState.troeError = true;  // make the TRoE write failure observable to the caller
    troeErrorStringSet("could not begin the transaction");
    pgConnectionRelease(connectionP);
    KT_RVE("pgTransactionBegin failed");
  }

  for (int ix = 0; ix < commands; ix++)
  {
    KT_T(KtSql, "SQL: %s;", sql[ix]);

    PGresult* res = PQexec(connectionP->connectionP, sql[ix]);
    if (res == NULL)
    {
      orionldState.troeError = true;  // no result - connection failure / OOM
      troeErrorStringSet(PQerrorMessage(connectionP->connectionP));
      KT_E("Database Error (PQexec returned NULL for SQL: %s)", sql[ix]);
      if (pgTransactionRollback(connectionP->connectionP) == false)
        KT_E("Database Error (pgTransactionRollback failed too)");
      pgConnectionInvalidate(connectionP);  // dead connection - drop it so the next borrow reconnects
      pgConnectionRelease(connectionP);
      return;
    }

    //
    // PQexec returns a non-NULL result even when the SQL statement itself failed (constraint
    // violation, deadlock, "current transaction is aborted", or a missing column because the TRoE
    // schema has not been migrated). Such failures leave the connection CONNECTION_OK, so they must
    // be caught here via the result status - otherwise the batch is silently lost and (for the Kafka
    // path) the offset committed regardless of the failure.
    //
    ExecStatusType execStatus = PQresultStatus(res);
    if ((execStatus != PGRES_COMMAND_OK) && (execStatus != PGRES_TUPLES_OK))
    {
      orionldState.troeError = true;
      troeErrorStringSet(PQresultErrorMessage(res));
      KT_E("Database Error (SQL command failed - status: %s, error: %s, SQL: %s)", PQresStatus(execStatus), PQresultErrorMessage(res), sql[ix]);
      PQclear(res);
      if (pgTransactionRollback(connectionP->connectionP) == false)
        KT_E("Database Error (pgTransactionRollback failed too)");
      pgConnectionRelease(connectionP);
      return;
    }
    PQclear(res);

    if (PQstatus(connectionP->connectionP) != CONNECTION_OK)
    {
      orionldState.troeError = true;  // connection dropped mid-batch
      troeErrorStringSet(PQerrorMessage(connectionP->connectionP));
      KT_E("SQL[%p]: bad connection: %d (%s)", connectionP->connectionP, PQstatus(connectionP->connectionP), PQerrorMessage(connectionP->connectionP));
      if (pgTransactionRollback(connectionP->connectionP) == false)
        KT_E("Database Error (pgTransactionRollback failed too)");
      pgConnectionInvalidate(connectionP);  // dead connection - drop it so the next borrow reconnects
      pgConnectionRelease(connectionP);
      return;
    }
  }

  if (pgTransactionCommit(connectionP->connectionP) != true)
  {
    orionldState.troeError = true;  // the COMMIT itself failed - the batch is not durable
    troeErrorStringSet("the transaction commit failed");
    KT_E("pgTransactionCommit failed");
  }

  pgConnectionRelease(connectionP);
}
