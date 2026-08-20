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
#include "orionld/troe/dbCreationCommand.h"                    // dbCreationCommand
#include "orionld/troe/pgTransactionBegin.h"                   // pgTransactionBegin
#include "orionld/troe/pgTransactionRollback.h"                // pgTransactionRollback
#include "orionld/troe/pgTransactionCommit.h"                  // pgTransactionCommit
#include "orionld/troe/pgDatabaseTableCreateAll.h"             // Own interface

#include <string.h>                                            // strcmp



// -----------------------------------------------------------------------------
//
// pgDatabaseTableCreateAll -
//
// FIXME: The types ValueType+OperationMode needs some investigation
//        It seems like they should be created ONCE for all DBs.
//        If this is true, the creation of the types must be part of the postgres installation.
//        OR: pgInit() creates the types being prepared for failures of type "already exists"
//
bool pgDatabaseTableCreateAll(PGconn* connectionP)
{
  if (pgTransactionBegin(connectionP) == false)
    KT_RE(false, "pgTransactionBegin failed");

  PGresult* res = PQexec(connectionP, dbCreationCommand);
  if (res == NULL)
  {
    pgTransactionRollback(connectionP);
    KT_RE(false, "Database Error (PQexec returned NULL for the TRoE schema creation)");
  }

  // A failed statement in the multi-command schema aborts the transaction but leaves a non-NULL result,
  // so the status must be checked here or the failure is swallowed (which left the broker running with
  // NO TRoE tables and no error logged - e.g. a GEOGRAPHY column when postgis is not installed).
  //
  // Exception: a "duplicate object" error is benign - the TRoE schema is simply already there (a broker
  // restart, or a pre-provisioned db, exactly the "already exists" case the header note anticipates). The
  // non-idempotent CREATE TYPEs make PQexec fail with duplicate_object; tolerate that, but let every
  // OTHER failure (missing postgis, bad DDL, ...) abort startup loudly.
  ExecStatusType execStatus = PQresultStatus(res);
  if ((execStatus != PGRES_COMMAND_OK) && (execStatus != PGRES_TUPLES_OK))
  {
    const char* sqlState     = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    bool        alreadyExists = (sqlState != NULL) &&
                                ((strcmp(sqlState, "42710") == 0) ||   // duplicate_object   (CREATE TYPE)
                                 (strcmp(sqlState, "42P07") == 0) ||   // duplicate_table    (CREATE TABLE / INDEX)
                                 (strcmp(sqlState, "42P06") == 0) ||   // duplicate_schema
                                 (strcmp(sqlState, "42723") == 0));    // duplicate_function

    if (alreadyExists)
    {
      KT_I("TRoE schema already present - reusing it (%s)", PQresultErrorMessage(res));
      PQclear(res);
      pgTransactionRollback(connectionP);   // the failed CREATE aborted the transaction - roll it back
      return true;                          // the schema is already there; not a failure
    }

    KT_E("Database Error (TRoE schema creation failed - status: %s, error: %s)", PQresStatus(execStatus), PQresultErrorMessage(res));
    PQclear(res);
    pgTransactionRollback(connectionP);
    return false;
  }
  PQclear(res);

  if (pgTransactionCommit(connectionP) == false)
    KT_RE(false, "Database Error (the TRoE schema creation could not be committed)");

  return true;
}
