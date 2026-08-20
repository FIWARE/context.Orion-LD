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
#include <stdio.h>                                             // snprintf

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/common/pqHeader.h"                           // Postgres header
#include "orionld/common/orionldState.h"                       // troeHost, pgPortString, troeUser, troePwd, troeSslMode, troeStmtTimeout
#include "orionld/troe/pgConnect.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// pgConnect - connect to a postgres database
//
// The connection parameters guard the connection pool against slots getting stuck:
// - connect_timeout:   a connection attempt fails after 5s instead of blocking for
//                      the OS TCP timeout (~2 minutes) while the pool slot is busy
// - keepalives:        a silently dead peer (network drop, backup/migration of the
//                      DB VM) is detected after ~60s instead of the kernel default
//                      of >2 hours - the blocked libpq call returns, the pool slot
//                      is freed
// - statement_timeout: a hung/runaway query is cancelled server-side after
//                      troeStmtTimeout ms (-troeStmtTimeout, 0 = disabled), instead
//                      of holding the pool slot forever
//
PGconn* pgConnect(const char* db)
{
  PGconn*  connectionP;
  int      attemptNo   = 0;
  int      maxAttempts = 30;
  char     options[128];
  char*    keywords[13];
  char*    values[13];
  int      kIx         = 0;

  keywords[kIx] = (char*) "host";                values[kIx] = troeHost;       ++kIx;
  keywords[kIx] = (char*) "port";                values[kIx] = pgPortString;   ++kIx;
  keywords[kIx] = (char*) "user";                values[kIx] = troeUser;       ++kIx;
  keywords[kIx] = (char*) "password";            values[kIx] = troePwd;        ++kIx;
  keywords[kIx] = (char*) "sslmode";             values[kIx] = troeSslMode;    ++kIx;
  keywords[kIx] = (char*) "connect_timeout";     values[kIx] = (char*) "5";    ++kIx;
  keywords[kIx] = (char*) "keepalives";          values[kIx] = (char*) "1";    ++kIx;
  keywords[kIx] = (char*) "keepalives_idle";     values[kIx] = (char*) "30";   ++kIx;
  keywords[kIx] = (char*) "keepalives_interval"; values[kIx] = (char*) "10";   ++kIx;
  keywords[kIx] = (char*) "keepalives_count";    values[kIx] = (char*) "3";    ++kIx;

  if (troeStmtTimeout > 0)
  {
    // idle_in_transaction_session_timeout: a session that begun a transaction but went
    // silent releases its locks (and, via pgConnectionRelease, its pool slot works again)
    snprintf(options, sizeof(options), "-c statement_timeout=%d -c idle_in_transaction_session_timeout=%d",
             troeStmtTimeout, troeStmtTimeout * 2);

    keywords[kIx] = (char*) "options";
    values[kIx]   = options;
    ++kIx;
  }

  if (db != NULL)
  {
    keywords[kIx] = (char*) "dbname";
    values[kIx]   = (char*) db;
    ++kIx;
  }

  keywords[kIx] = NULL;
  values[kIx]   = NULL;

  while (attemptNo < maxAttempts)
  {
    connectionP = PQconnectdbParams(keywords, values, 0);  // 0: no expansion of dbname - see https://www.postgresql.org/docs/12/libpq-connect.html

    ++attemptNo;

    if (connectionP == NULL)  // Only on OOM
    {
      sleep(1);
      continue;
    }

    if (PQstatus(connectionP) == CONNECTION_OK)
      break;

    // Connection failed - log once, clean up, and retry
    if (attemptNo == 1)
      KT_W("Waiting for postgres: %s", PQerrorMessage(connectionP));

    PQfinish(connectionP);
    connectionP = NULL;
    sleep(1);
  }

  if (connectionP == NULL)
    KT_RE(NULL, "Database Error (unable to connect to postgres(host:'%s', port:%s, user:'%s', pwd:'%s', db:'%s') after %d attempts",
          troeHost, pgPortString, troeUser, troePwd, db, maxAttempts);

  return connectionP;
}
