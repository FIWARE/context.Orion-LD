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
#include "orionld/common/orionldState.h"                       // troeHost, pgPortString, troeUser, troePwd, troeSslMode
#include "orionld/troe/pgConnect.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// pgConnect - connect to a postgres database
//
PGconn* pgConnect(const char* db)
{
  PGconn*  connectionP;
  int      attemptNo   = 0;
  int      maxAttempts = 30;
  char*    keywords[8] = { (char*) "host",   (char*) "port",       (char*) "user",   (char*) "password",  (char*) "sslmode",  NULL, NULL, NULL };
  char*    values[8]   = { troeHost,         pgPortString,         troeUser,         troePwd,             troeSslMode,        NULL, NULL, NULL };

  if (db != NULL)
  {
    keywords[5] = (char*) "dbname";
    values[5]   = (char*) db;
  }

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
