/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
* Author: Carsten Frey
*/
#include <stdlib.h>                                             // atoi
#include <stdio.h>                                             // snprintf

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/common/pqHeader.h"                           // Postgres header
#include "orionld/troe/pgTransactionBegin.h"                   // pgTransactionBegin
#include "orionld/troe/pgTransactionRollback.h"                // pgTransactionRollback
#include "orionld/troe/pgTransactionCommit.h"                  // pgTransactionCommit
#include "orionld/troe/pgSchemaMigrate.h"                      // Own interface, PG_SCHEMA_VERSION



// -----------------------------------------------------------------------------
//
// PG_SCHEMA_LOCK_KEY - advisory-lock key serializing migrations across broker instances
//
// Arbitrary but fixed - every broker uses the same key, so only one of them migrates a
// given database at a time (pg_advisory_lock is keyed per database).
//
#define PG_SCHEMA_LOCK_KEY 947113287



// -----------------------------------------------------------------------------
//
// PgMigrationStep - one step that brings the schema from version (toVersion - 1) to toVersion
//
// IMPORTANT: 'sql' MUST be idempotent (ADD COLUMN IF NOT EXISTS, CREATE INDEX IF NOT EXISTS, ...).
//            Freshly created databases already carry the latest table layout, so the steps run
//            as no-ops on them - they only serve to upgrade pre-existing databases.
//
typedef struct PgMigrationStep
{
  int          toVersion;
  const char*  description;
  const char*  sql;
} PgMigrationStep;



// -----------------------------------------------------------------------------
//
// pgMigrationSteps - the ordered list of schema migrations
//
// To add a new layout change: bump PG_SCHEMA_VERSION (in pgSchemaMigrate.h) and append a step here.
//
static const PgMigrationStep pgMigrationSteps[] =
{
  {
    2,
    "write correlator column on entities/attributes/subAttributes (+ index)",
    "ALTER TABLE entities      ADD COLUMN IF NOT EXISTS correlator TEXT;"
    "ALTER TABLE attributes    ADD COLUMN IF NOT EXISTS correlator TEXT;"
    "ALTER TABLE subAttributes ADD COLUMN IF NOT EXISTS correlator TEXT;"
    "CREATE INDEX IF NOT EXISTS attributes_correlator_index ON attributes (correlator);"
  }
};

static const int pgMigrationStepsNo = (int) (sizeof(pgMigrationSteps) / sizeof(pgMigrationSteps[0]));



// -----------------------------------------------------------------------------
//
// pgCommandRun - run a single SQL command (no result rows expected)
//
static bool pgCommandRun(PGconn* connectionP, const char* sql)
{
  PGresult* res = PQexec(connectionP, sql);

  if ((res == NULL) || (PQresultStatus(res) != PGRES_COMMAND_OK))
  {
    KT_E("Database Error (running '%s': %s)", sql, PQerrorMessage(connectionP));
    if (res != NULL)
      PQclear(res);
    return false;
  }

  PQclear(res);
  return true;
}



// -----------------------------------------------------------------------------
//
// pgSchemaVersionGet - read the stored schema version from the 'metadata' table
//
// Returns 1 (the first versioned layout) if the table or the row is absent - i.e. for
// databases created before this versioning mechanism existed.
//
static int pgSchemaVersionGet(PGconn* connectionP)
{
  PGresult* res = PQexec(connectionP, "SELECT value FROM metadata WHERE name = 'schemaVersion'");

  if ((res == NULL) || (PQresultStatus(res) != PGRES_TUPLES_OK) || (PQntuples(res) == 0))
  {
    if (res != NULL)
      PQclear(res);
    return 1;
  }

  int version = atoi(PQgetvalue(res, 0, 0));
  PQclear(res);

  return (version < 1)? 1 : version;
}



// -----------------------------------------------------------------------------
//
// pgSchemaVersionSet - store the schema version in the 'metadata' table (upsert)
//
static bool pgSchemaVersionSet(PGconn* connectionP, int version)
{
  char sql[256];

  snprintf(sql, sizeof(sql),
           "INSERT INTO metadata (name, value) VALUES ('schemaVersion', '%d') "
           "ON CONFLICT (name) DO UPDATE SET value = '%d'",
           version, version);

  return pgCommandRun(connectionP, sql);
}



// -----------------------------------------------------------------------------
//
// pgSchemaMigrate -
//
bool pgSchemaMigrate(PGconn* connectionP)
{
  char lockSql[64];

  // Serialize migrations across concurrent broker instances on the same database (taken first,
  // so even the 'metadata' table creation below cannot race between two starting brokers).
  snprintf(lockSql, sizeof(lockSql), "SELECT pg_advisory_lock(%d)", PG_SCHEMA_LOCK_KEY);
  PGresult* lockRes = PQexec(connectionP, lockSql);
  if (lockRes != NULL)
    PQclear(lockRes);

  bool ok = true;

  // The metadata table holds the schema version (and any future broker-managed metadata)
  if (pgCommandRun(connectionP, "CREATE TABLE IF NOT EXISTS metadata (name TEXT PRIMARY KEY, value TEXT)") == false)
  {
    KT_E("Database Error (unable to create the TRoE 'metadata' table)");
    ok = false;
  }
  else
  {
    int currentVersion = pgSchemaVersionGet(connectionP);

    for (int ix = 0; ix < pgMigrationStepsNo; ix++)
    {
      if (pgMigrationSteps[ix].toVersion <= currentVersion)
        continue;

      KT_I("TRoE schema migration: applying v%d (%s)", pgMigrationSteps[ix].toVersion, pgMigrationSteps[ix].description);

      if (pgTransactionBegin(connectionP) == false)
      {
        KT_E("Database Error (pgTransactionBegin failed during schema migration)");
        ok = false;
        break;
      }

      if ((pgCommandRun(connectionP, pgMigrationSteps[ix].sql) == false) ||
          (pgSchemaVersionSet(connectionP, pgMigrationSteps[ix].toVersion) == false))
      {
        pgTransactionRollback(connectionP);
        KT_E("Database Error (TRoE schema migration to v%d failed - rolled back)", pgMigrationSteps[ix].toVersion);
        ok = false;
        break;
      }

      pgTransactionCommit(connectionP);
      currentVersion = pgMigrationSteps[ix].toVersion;
      KT_I("TRoE schema migration: now at v%d", currentVersion);
    }
  }

  // Release the advisory lock
  char unlockSql[64];
  snprintf(unlockSql, sizeof(unlockSql), "SELECT pg_advisory_unlock(%d)", PG_SCHEMA_LOCK_KEY);
  PGresult* unlockRes = PQexec(connectionP, unlockSql);
  if (unlockRes != NULL)
    PQclear(unlockRes);

  return ok;
}
