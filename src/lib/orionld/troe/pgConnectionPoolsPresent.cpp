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
#include "kbase/kMacros.h"                                     // K_FT
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/troe/PgConnection.h"                         // PgConnection
#include "orionld/troe/PgConnectionPool.h"                     // PgConnectionPool
#include "orionld/troe/pgConnectionPools.h"                    // pgPoolMaster



// -----------------------------------------------------------------------------
//
// pgConnectionPoolPresent -
//
static void pgConnectionPoolPresent(PgConnectionPool* poolP)
{
#ifdef DEBUG
  LM(("PGPOOL: Postgres Connection Pool for DB '%s'", poolP->db));
  LM(("PGPOOL:   Size of pool:   %d", poolP->items));

  for (int ix = 0; ix < poolP->items; ix++)
  {
    if (poolP->connectionV[ix] == NULL)
      continue;

    PgConnection* cP = poolP->connectionV[ix];

    LM(("PGPOOL:  Connection %d:", ix));
    LM(("PGPOOL:    busy:       %s", K_FT(cP->busy)));
    LM(("PGPOOL:    uses:       %d", cP->uses));
    LM(("PGPOOL:    connection: %p", cP->connectionP));
    LM(("PGPOOL:"));
  }
#endif
}



// -----------------------------------------------------------------------------
//
// pgConnectionPoolsPresent -
//
void pgConnectionPoolsPresent(void)
{
  PgConnectionPool* poolP = pgPoolMaster;

  while (poolP != NULL)
  {
    pgConnectionPoolPresent(poolP);
    poolP = poolP->next;
  }
}
