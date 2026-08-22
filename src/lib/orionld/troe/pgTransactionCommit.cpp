/*
*
* Copyright 2020 FIWARE Foundation e.V.
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
#include <string.h>                                            // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/common/pqHeader.h"                           // Postgres header
#include "orionld/troe/pgTransactionCommit.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// pgTransactionCommit - commit a transaction
//
bool pgTransactionCommit(PGconn* connectionP)
{
  PGresult* res;

  res = PQexec(connectionP, "COMMIT");
  if (res == NULL)
    KT_RE(false, "Database Error (PQexec(COMMIT): %s)", PQerrorMessage(connectionP));

  if (PQresultStatus(res) != PGRES_COMMAND_OK)  // e.g. serialization failure at commit time
  {
    KT_E("Database Error (COMMIT failed: %s)", PQresultErrorMessage(res));
    PQclear(res);
    return false;
  }

  //
  // A COMMIT inside an aborted transaction succeeds with PGRES_COMMAND_OK, but the server
  // actually performs a ROLLBACK (visible in the command tag) - nothing was made durable.
  //
  if (strcmp(PQcmdStatus(res), "ROLLBACK") == 0)
  {
    KT_E("Database Error (COMMIT was answered with ROLLBACK - aborted transaction, nothing is durable)");
    PQclear(res);
    return false;
  }

  PQclear(res);

  if (PQstatus(connectionP) != CONNECTION_OK)
  {
    KT_E("Database Error (bad connection after COMMIT: %d - %s)", PQstatus(connectionP), PQerrorMessage(connectionP));
    return false;
  }

  PGTransactionStatusType st;
  if ((st = PQtransactionStatus(connectionP)) != PQTRANS_IDLE)
  {
    KT_E("Database Error (transaction status %d after COMMIT - the batch may not be durable)", st);
    return false;
  }

  return true;
}
