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
* Author: Ken Zangelin
*/
#include <stdlib.h>                                              // free

extern "C"
{
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
}

#include "orionld/common/orionldState.h"                         // orionldState, orionldStateRelease
#include "orionld/mongoc/mongocConnectionRelease.h"              // mongocConnectionRelease
#include "orionld/notifications/orionldAlterationsTreat.h"       // orionldAlterationsTreat
#include "orionld/ws/wsRequestCleanup.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// wsRequestCleanup - lightweight cleanup for WS message handlers
//
// This replaces requestCompleted() for WS operations.  requestCompleted() is
// MHD's per-connection callback and assumes a ConnectionInfo from *con_cls,
// statistics/metrics tracking, CURL cleanup, etc. - none of which apply to WS.
//
// Calling requestCompleted() from a WS handler trashes the thread-local
// orionldState that MHD still needs for the original upgrade connection,
// causing a free()-of-invalid-pointer crash when MHD later calls
// requestCompleted() itself.
//
// What we DO need after a WS service-routine call:
//   1. Process notification alterations (if any)
//   2. Release mongoc collections and connection
//   3. Free malloc'd payload buffer (if any)
//   4. Release delayed-free buffers (orionldStateRelease)
//   5. Reset the kalloc bump allocator
//
void wsRequestCleanup(void)
{
  // 1. Process any notification alterations
  if (orionldState.alterations != NULL)
  {
    orionldAlterationsTreat(orionldState.alterations);
    orionldState.alterations = NULL;  // Prevent MHD's requestCompleted from reprocessing freed data
  }

  // 2. Release mongoc collections and connection (same pattern as rest.cpp)
  mongoc_collection_t* contextsP      = orionldState.mongoc.contextsP;
  mongoc_collection_t* entitiesP      = orionldState.mongoc.entitiesP;
  mongoc_collection_t* subscriptionsP = orionldState.mongoc.subscriptionsP;
  mongoc_collection_t* registrationsP = orionldState.mongoc.registrationsP;

  orionldState.mongoc.contextsP      = NULL;
  orionldState.mongoc.entitiesP      = NULL;
  orionldState.mongoc.subscriptionsP = NULL;
  orionldState.mongoc.registrationsP = NULL;

  if (contextsP      != NULL)  mongoc_collection_destroy(contextsP);
  if (entitiesP      != NULL)  mongoc_collection_destroy(entitiesP);
  if (subscriptionsP != NULL)  mongoc_collection_destroy(subscriptionsP);
  if (registrationsP != NULL)  mongoc_collection_destroy(registrationsP);

  mongocConnectionRelease();

  // 3. Free malloc'd payload buffer (if any)
  if ((orionldState.in.payload != NULL) && (orionldState.in.payload != orionldState.preallocReqBuf))
  {
    free(orionldState.in.payload);
    orionldState.in.payload = NULL;
  }

  // 4. Release delayed-free buffers
  orionldStateRelease();

  // 5. Reset the kalloc bump allocator so it's clean for the next WS message
  kaBufferReset(&orionldState.kalloc, false);
}
