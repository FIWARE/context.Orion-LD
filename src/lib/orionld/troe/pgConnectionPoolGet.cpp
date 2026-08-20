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
#include <string.h>                                            // strcmp
#include <pthread.h>                                           // pthread_mutex_t

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/types/PgConnectionPool.h"                    // PgConnectionPool
#include "orionld/troe/pgConnectionPools.h"                    // pgPoolMaster
#include "orionld/troe/pgConnectionPoolCreate.h"               // pgConnectionPoolCreate
#include "orionld/troe/pgConnectionPoolInsert.h"               // pgConnectionPoolInsert
#include "orionld/troe/pgConnectionPoolGet.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// poolListMutex - protects the linked list of connection pools
//
// Without it, two threads (e.g. the Kafka consumers) that use a database for the
// first time simultaneously would both create a pool for it - one insert gets lost,
// and list walkers can see a torn list.
//
static pthread_mutex_t poolListMutex = PTHREAD_MUTEX_INITIALIZER;



// -----------------------------------------------------------------------------
//
// pgConnectionPoolGet -
//
PgConnectionPool* pgConnectionPoolGet(char* db)
{
  //
  // The default db (with name NULL) has its pool as the very first pool in the pool list
  // (created before any threads exist - never modified after that)
  //
  if (db == NULL)
    return pgPoolMaster;

  pthread_mutex_lock(&poolListMutex);

  PgConnectionPool* poolP = pgPoolMaster->next;

  while (poolP != NULL)
  {
    if (strcmp(poolP->db, db) == 0)
    {
      pthread_mutex_unlock(&poolListMutex);
      return poolP;
    }

    poolP = poolP->next;
  }

  // No pool found, will have to create a new one
  poolP = pgConnectionPoolCreate(db, pgPoolMaster->items);
  if (poolP == NULL)
  {
    pthread_mutex_unlock(&poolListMutex);
    KT_RE(NULL, "Database Error (unable to create connection pool for db '%s')", db);
  }

  pgConnectionPoolInsert(poolP);
  pthread_mutex_unlock(&poolListMutex);

  return poolP;
}
