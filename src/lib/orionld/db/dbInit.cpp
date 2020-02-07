/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include "orionld/db/dbConfiguration.h"                                    // This is where the DB is selected

#if DB_DRIVER_MONGO_CPP_LEGACY

#include "orionld/mongoCppLegacy/mongoCppLegacyInit.h"                     // mongoCppLegacyInit
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityUpdate.h"             // mongoCppLegacyEntityUpdate
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityLookup.h"             // mongoCppLegacyEntityLookup
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityAttributeLookup.h"    // mongoCppLegacyEntityAttributeLookup
#include "orionld/mongoCppLegacy/mongoCppLegacyKjTreeFromBsonObj.h"        // mongoCppLegacyKjTreeFromBsonObj
#include "orionld/mongoCppLegacy/mongoCppLegacyKjTreeToBsonObj.h"          // mongoCppLegacyKjTreeToBsonObj
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityBatchDelete.h"        // mongoCppLegacyEntityBatchDelete
#include "orionld/mongoCppLegacy/mongoCppLegacySubscriptionMatchEntityIdAndAttributes.h"   // mongoCppLegacySubscriptionMatchEntityIdAndAttributes
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityListLookupWithIdTypeCreDate.h"        // mongoCppLegacyEntityListLookupWithIdTypeCreDate
#include "orionld/mongoCppLegacy/mongoCppLegacyRegistrationLookup.h"       // mongoCppLegacyRegistrationLookup
#include "orionld/mongoCppLegacy/mongoCppLegacyRegistrationExists.h"       // mongoCppLegacyRegistrationExists
#include "orionld/mongoCppLegacy/mongoCppLegacyRegistrationDelete.h"       // mongoCppLegacyRegistrationDelete

#elif DB_DRIVER_MONGOC
#include "orionld/mongoc/mongocInit.h"                                     // mongocInit
#include "orionld/mongoc/mongocEntityUpdate.h"                             // mongocEntityUpdate
#include "orionld/mongoc/mongocEntityLookup.h"                             // mongocEntityLookup
#include "orionld/mongoc/mongocKjTreeFromBson.h"                           // mongocKjTreeFromBson
#endif
#include "orionld/db/dbInit.h"                                             // Own interface



// ----------------------------------------------------------------------------
//
// dbInit -
//
// PARAMETERS
//   dbHost - the host and port where the mongobd server runs. E.g. "localhost:27017"
//   dbName - the name of the database. Default is 'orion'
//
void dbInit(const char* dbHost, const char* dbName)
{
#if DB_DRIVER_MONGO_CPP_LEGACY

  dbEntityLookup                           = mongoCppLegacyEntityLookup;
  dbEntityAttributeLookup                  = mongoCppLegacyEntityAttributeLookup;
  dbEntityUpdate                           = mongoCppLegacyEntityUpdate;
  dbDataToKjTree                           = mongoCppLegacyKjTreeFromBsonObj;
  dbDataFromKjTree                         = mongoCppLegacyKjTreeToBsonObj;
  dbEntityBatchDelete                      = mongoCppLegacyEntityBatchDelete;
  dbSubscriptionMatchEntityIdAndAttributes = mongoCppLegacySubscriptionMatchEntityIdAndAttributes;
  dbEntityListLookupWithIdTypeCreDate      = mongoCppLegacyEntityListLookupWithIdTypeCreDate;
  dbRegistrationLookup                     = mongoCppLegacyRegistrationLookup;
  dbRegistrationExists                     = mongoCppLegacyRegistrationExists;
  dbRegistrationDelete                     = mongoCppLegacyRegistrationDelete;

  mongoCppLegacyInit(dbHost, dbName);

#elif DB_DRIVER_MONGOC

  dbEntityLookup                           = mongocEntityLookup;
  dbEntityUpdate                           = mongocEntityUpdate;
  dbDataToKjTree                           = mongocKjTreeFromBsonObj;
  dbDataFromKjTree                         = NULL;  // FIXME: Implement mongocKjTreeToBson
  dbEntityBatchDelete                      = NULL;  // FIXME: Implement mongocEntityBatchDelete
  dbSubscriptionMatchEntityIdAndAttributes = NULL;  // FIXME: Implement mongocSubscriptionMatchEntityIdAndAttributes
  dbEntityListLookupWithIdTypeCreDate      = NULL;  // FIXME: Implement mongocEntityListLookupWithIdTypeCreDate
  dbRegistrationLookup                     = NULL;  // FIXME: Implement mongocRegistrationLookup
  dbRegistrationExists                     = NULL;  // FIXME: Implement mongocRegistrationExists
  dbRegistrationDelete                     = NULL;  // FIXME: Implement mongocRegistrationDelete

  mongocInit(dbHost, dbName);

#else
  #error Please define either DB_DRIVER_MONGO_CPP_LEGACY or DB_DRIVER_MONGOC in src/lib/orionld/db/dbConfiguration.h
#endif
}
