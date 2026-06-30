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
    KT_RVE("no connection to postgres");
  }

  if (pgTransactionBegin(connectionP->connectionP) != true)
  {
    orionldState.troeError = true;  // make the TRoE write failure observable to the caller
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
      KT_E("Database Error (no result from PQexec)");
      if (pgTransactionRollback(connectionP->connectionP) == false)
        KT_E("Database Error (pgTransactionRollback failed too)");
      pgConnectionRelease(connectionP);
      return;
    }

    //
    // PQexec returns a non-NULL result also on a failed SQL command (constraint violation,
    // deadlock, "current transaction is aborted", ...). Such failures leave the connection
    // CONNECTION_OK, so they must be caught here via the result status - otherwise the batch
    // would be silently lost and (for the Kafka path) the offset committed regardless.
    //
    ExecStatusType pgStatus = PQresultStatus(res);
    if ((pgStatus != PGRES_COMMAND_OK) && (pgStatus != PGRES_TUPLES_OK))
    {
      orionldState.troeError = true;
      KT_E("Database Error (%s): %s", PQresStatus(pgStatus), PQerrorMessage(connectionP->connectionP));
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
      KT_E("SQL[%p]: bad connection: %d", connectionP->connectionP, PQstatus(connectionP->connectionP));  // FIXME: string! (last error?)
      if (pgTransactionRollback(connectionP->connectionP) == false)
        KT_E("Database Error (pgTransactionRollback failed too)");
      pgConnectionRelease(connectionP);
      return;
    }
  }

  if (pgTransactionCommit(connectionP->connectionP) != true)
  {
    orionldState.troeError = true;  // the COMMIT itself failed - the batch is not durable
    KT_E("pgTransactionCommit failed");
  }

  pgConnectionRelease(connectionP);
}
