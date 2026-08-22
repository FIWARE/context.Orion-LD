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

#include "orionld/types/PgConnectionPool.h"                    // PgConnectionPool
#include "orionld/types/PgConnection.h"                        // PgConnection
#include "orionld/common/orionldState.h"                       // troeHost, pgPortString, troeUser, troePwd
#include "orionld/troe/pgConnect.h"                            // pgConnect
#include "orionld/troe/pgConnectionPoolGet.h"                  // pgConnectionPoolGet
#include "orionld/troe/pgSem.h"                                // pgSemWait, pgSemTimedWait
#include "orionld/troe/pgConnectionGet.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// POOL_FREE_SLOT_TIMEOUT - seconds to wait for a pool slot to become free
//
// A slot is normally borrowed only for the duration of one SQL batch/query, so under
// healthy conditions the wait is ~zero. The timeout only expires when every slot is
// held by a long-running or stuck operation - in that case the caller gets NULL
// (-> "no connection to postgres") instead of blocking forever.
//
#define POOL_FREE_SLOT_TIMEOUT 10



// -----------------------------------------------------------------------------
//
// wsTrim -
//
static char* wsTrim(char* s)
{
  // trim initial whitespace
  while ((*s == ' ') || (*s == '\t') || (*s == '\n'))
    ++s;

  // trim trailing whitespace
  int last = strlen(s);
  while ((last > 0) && ((s[last - 1] == ' ') || (s[last - 1] == '\t') || (s[last - 1] == '\n')))
    --last;
  s[last] = 0;

  return s;
}



// -----------------------------------------------------------------------------
//
// poolStateLog - log the state of every slot in the pool (for pool-exhaustion analysis)
//
// The backend PID makes the slot findable in pg_stat_activity on the postgres side:
//   SELECT pid, state, query_start, query FROM pg_stat_activity WHERE pid = <backendPid>;
//
// Must be called with poolP->poolSem taken.
// Busy slots are in concurrent use by their borrowing thread - PQstatus/PQtransactionStatus/
// PQbackendPID only read plain fields of the PGconn, which is acceptable for diagnostics.
//
static void poolStateLog(PgConnectionPool* poolP)
{
  for (int ix = 0; ix < poolP->items; ix++)
  {
    PgConnection* cP = poolP->connectionV[ix];

    if (cP == NULL)
      KT_E("  slot %02d: empty", ix);
    else if (cP->connectionP == NULL)
      KT_E("  slot %02d: busy=%d, uses=%d, not connected", ix, cP->busy, cP->uses);
    else
    {
      KT_E("  slot %02d: busy=%d, uses=%d, pgStatus=%d, txStatus=%d, backendPid=%d",
           ix, cP->busy, cP->uses, PQstatus(cP->connectionP), PQtransactionStatus(cP->connectionP), PQbackendPID(cP->connectionP));
    }
  }
}



// -----------------------------------------------------------------------------
//
// pgConnectionGet -
//
// Slot accounting:
//   poolP->queueSem counts the free slots of the pool (initialized to poolP->items).
//   - Borrowing a connection consumes one unit (the sem_wait at the top).
//   - The unit is given back by pgConnectionRelease - or right here, on every path
//     that fails AFTER the wait and thus does NOT hand out a slot.
//   poolP->poolSem is a binary semaphore protecting connectionV[] and the busy flags.
//
// NEVER wait on queueSem while holding poolSem - the release of queueSem units
// (pgConnectionRelease) takes poolSem, so that order would deadlock.
//
PgConnection* pgConnectionGet(const char* db)
{
  char* _db = (char*) db;

  //
  // Empty tenant => use default db name
  //
  // Note that a non-NULL 'db' that points to an empty string "" corresponds to the default database (orion)
  // Something very different is db == NULL - the "default" postgres database - to where we need to connect to create new databases
  //
  if ((db != NULL) && (*db == 0))
    _db = dbName;

  if (_db != NULL)
    _db = wsTrim(_db);

  PgConnectionPool* poolP = pgConnectionPoolGet(_db);  // pgConnectionPoolGet creates the pool if it doesn't already exist

  if (poolP == NULL)
    KT_RE(NULL, "unable to obtain a connection pool reference");

  //
  // Await a free slot in the pool (bounded - a wedged pool must not block callers forever)
  //
  if (pgSemTimedWait(&poolP->queueSem, POOL_FREE_SLOT_TIMEOUT) == false)
  {
    KT_E("TRoE connection pool for '%s' exhausted - all %d slots busy for more than %d seconds. Slot state:",
         (poolP->db == NULL)? "(default)" : poolP->db, poolP->items, POOL_FREE_SLOT_TIMEOUT);

    pgSemWait(&poolP->poolSem);
    poolStateLog(poolP);
    sem_post(&poolP->poolSem);

    return NULL;
  }

  // Await the right to modify the pool
  pgSemWait(&poolP->poolSem);

  // Search for a free but already connected PgConnection in the pool
  for (int ix = 0; ix < poolP->items; ix++)
  {
    PgConnection* cP = poolP->connectionV[ix];

    if ((cP == NULL) || (cP->busy == true))
      continue;

    if (cP->connectionP != NULL)
    {
      // check if we are still connected
      ConnStatusType pgStatus = PQstatus(cP->connectionP);
      if (pgStatus != CONNECTION_OK)
      {
        KT_W("Connection of item %d is lost, trying to re-connect...", ix);
        // try to re-connect
        PQreset(cP->connectionP);
        // get status again
        pgStatus = PQstatus(cP->connectionP);

        // if still no connection
        if (pgStatus != CONNECTION_OK)
        {
          // Close the libpq connection before freeing the wrapper: PQreset leaves the PGconn
          // allocated on failure, so free()ing the wrapper alone leaked the socket + memory.
          PQfinish(cP->connectionP);
          // we free this pointer that it can be used in the next call of pgConnectionGet
          free(poolP->connectionV[ix]);
          poolP->connectionV[ix] = NULL;
          KT_W("Connection failed, pointer of item %d was re-set to NULL (%p)", ix, poolP->connectionV[ix]);
          // this time no success finding a connection that is working, try in the next loop
          continue;
        }
      }

      // Great - found a free and already connected item - let's use it !
      cP->busy = true;

      sem_post(&poolP->poolSem);

      cP->uses += 1;
      return cP;
    }
  }

  //
  // No already connected item was found - just look for an unused item and connect it to postgres
  //
  PgConnection* cP = NULL;
  for (int ix = 0; ix < poolP->items; ix++)
  {
    if (poolP->connectionV[ix] == NULL)
    {
      //
      // We found a completely unused slot - need to allocate
      // Pity doing this with the semaphore taken ...
      // But, there's no other choice as the slot must be marked as 'busy' before the sem can be released
      //
      poolP->connectionV[ix] = (PgConnection*) calloc(1, sizeof(PgConnection));
      if (poolP->connectionV[ix] == NULL)
      {
        sem_post(&poolP->poolSem);
        sem_post(&poolP->queueSem);  // give the consumed free-slot unit back
        KT_RE(NULL, "Out of memory (unable to allocate room for a Postgres Connection - %d bytes)", sizeof(PgConnection));
      }

      cP = poolP->connectionV[ix];
      cP->poolP = poolP;
      break;
    }
    else if (poolP->connectionV[ix]->busy == false)
    {
      cP = poolP->connectionV[ix];
      break;
    }
  }


  if (cP != NULL)
    cP->busy = true;  // Now the pool item 'cP' is ours - after this we can let go of the semaphore

  sem_post(&poolP->poolSem);

  if (cP == NULL)
  {
    //
    // Can't happen: queueSem guarantees that at least one slot is free (empty or not busy).
    // Kept as a guard - and the consumed unit is given back so the pool doesn't shrink.
    //
    sem_post(&poolP->queueSem);

    KT_W("Internal Error (bug in postgres connection pool logic?)");
    KT_W("poolP at %p", poolP);
    KT_W("poolP->items: %d", poolP->items);
    KT_W("poolP->connectionV at %p", poolP->connectionV);

    return NULL;
  }

  if (cP->connectionP == NULL)  // Virgin connection
  {
    cP->connectionP = pgConnect(_db);
    if (cP->connectionP == NULL)
    {
      pgSemWait(&poolP->poolSem);
      cP->busy = false;  // So the slot can be used again!
      sem_post(&poolP->poolSem);
      sem_post(&poolP->queueSem);  // give the consumed free-slot unit back

      KT_RE(NULL, "Database Error (unable to connect to postgres(%s))", _db);
    }
    else
    {
      // check if we are connected
      ConnStatusType pgStatus = PQstatus(cP->connectionP);
      if (pgStatus != CONNECTION_OK)
      {
        // log the PG error message before closing the connection (the string lives inside the PGconn)
        KT_E("Database Connection could not be established (%s): %s", _db, PQerrorMessage(cP->connectionP));

        pgSemWait(&poolP->poolSem);

        // find the connection pointer in the pool, close its connection and free it
        for (int ix = 0; ix < poolP->items; ix++)
        {
          if (poolP->connectionV[ix] == cP)
          {
            PQfinish(cP->connectionP);
            free(poolP->connectionV[ix]);
            poolP->connectionV[ix] = NULL;
            break;
          }
        }

        sem_post(&poolP->poolSem);
        sem_post(&poolP->queueSem);  // give the consumed free-slot unit back

        return NULL;
      }
    }
  }

  cP->uses += 1;
  return cP;
}
