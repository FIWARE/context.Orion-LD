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
// pgSchemaMigrate -
//
// Brings a single TRoE PostgreSQL database up to PG_SCHEMA_VERSION by applying any
// pending migration steps. Safe to call on every startup and for every tenant DB:
// it is idempotent, records the applied version in the 'metadata' table, and takes
// a PostgreSQL advisory lock so concurrent broker instances don't migrate in parallel.
//
extern bool pgSchemaMigrate(PGconn* connectionP);

#endif  // SRC_LIB_ORIONLD_TROE_PGSCHEMAMIGRATE_H_
