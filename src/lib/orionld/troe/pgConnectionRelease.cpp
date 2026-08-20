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
extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/common/pqHeader.h"                           // Postgres header
#include "orionld/types/PgConnection.h"                        // PgConnection
#include "orionld/types/PgConnectionPool.h"                    // PgConnectionPool
#include "orionld/troe/pgSem.h"                                // pgSemWait
#include "orionld/troe/pgConnectionRelease.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// pgConnectionRelease - release a connection to a postgres database
//
// Cleanup is done while the slot is still ours (busy == true - no other thread touches it):
// - A connection left inside a transaction (a failed COMMIT, a ROLLBACK that never ran)
//   would poison the next borrower - every statement would fail with
//   "current transaction is aborted". Roll it back here.
// - A dead connection is closed right away, so its socket isn't leaked; the empty slot
//   is re-connected by the next pgConnectionGet.
//
// Then the slot is handed back: busy=false under the pool semaphore, and one unit is
// posted to queueSem - waking up a caller that's waiting for a free slot.
//
void pgConnectionRelease(PgConnection* connectionP)
{
  if (connectionP == NULL)
    return;

  PGconn* conn = connectionP->connectionP;

  if (conn != NULL)
  {
    if (PQstatus(conn) != CONNECTION_OK)
    {
      PQfinish(conn);
      connectionP->connectionP = NULL;
    }
    else
    {
      PGTransactionStatusType txStatus = PQtransactionStatus(conn);

      if ((txStatus == PQTRANS_INTRANS) || (txStatus == PQTRANS_INERROR))
      {
        KT_W("connection released inside a transaction (status: %d) - rolling back", txStatus);

        PGresult* res = PQexec(conn, "ROLLBACK");
        if (res != NULL)
          PQclear(res);
      }
    }
  }

  // Return the connection to its pool
  PgConnectionPool* poolP = connectionP->poolP;

  if (poolP != NULL)
  {
    pgSemWait(&poolP->poolSem);
    connectionP->busy = false;
    sem_post(&poolP->poolSem);

    sem_post(&poolP->queueSem);  // one more free slot
  }
  else
    connectionP->busy = false;
}
