#ifndef SRC_LIB_ORIONLD_TROE_PGSCHEMAMIGRATE_H_
#define SRC_LIB_ORIONLD_TROE_PGSCHEMAMIGRATE_H_

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
#include "orionld/common/pqHeader.h"                           // PGconn



// -----------------------------------------------------------------------------
//
// PG_SCHEMA_VERSION - the schema version this build of the broker expects
//
// Bump this and add a matching step to 'pgMigrationSteps' (in pgSchemaMigrate.cpp)
// whenever the TRoE PostgreSQL layout changes.
//
#define PG_SCHEMA_VERSION 2



// -----------------------------------------------------------------------------
//
// pgSchemaExists - true if the TRoE tables already exist in the connected database
//
// Must be called BEFORE the tables are (re-)created, so pgSchemaMigrate can tell a
// brand-new database (latest layout) from a pre-existing one (possibly outdated).
//
extern bool pgSchemaExists(PGconn* connectionP);



// -----------------------------------------------------------------------------
//
// pgSchemaMigrate -
//
// Checks a single TRoE PostgreSQL database's schema version against PG_SCHEMA_VERSION.
//
//   - brand-new database (schemaPreExisted == false)  -> stamp it at the current version
//   - already at the current version                  -> nothing to do
//   - outdated, and the broker was started with -migrate (the 'migrate' CLI option)
//                                                     -> apply the pending, idempotent steps
//   - outdated, and -migrate was NOT given            -> log how to migrate and EXIT the broker
//
// A PostgreSQL advisory lock serializes concurrent broker instances. The applied version is
// recorded in the 'metadata' table.
//
extern bool pgSchemaMigrate(PGconn* connectionP, const char* dbName, bool schemaPreExisted);

#endif  // SRC_LIB_ORIONLD_TROE_PGSCHEMAMIGRATE_H_
