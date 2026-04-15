/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include <stdlib.h>                                         // malloc
#include <string.h>                                         // strdup
#include <strings.h>                                        // bzero

#include "orionld/types/DdsService.h"                       // DdsService
#include "orionld/common/orionldState.h"                    // ddsServices



// -----------------------------------------------------------------------------
//
// ddsServiceCreate
//
DdsService* ddsServiceCreate
(
  const char* name,
  const char* requestType,
  const char* requestQoS,
  const char* replyType,
  const char* replyQoS,
  const char* entityId,
  const char* entityType,
  const char* attributeName
)
{
  DdsService* sP = (DdsService*) malloc(sizeof(DdsService));

  bzero(sP, sizeof(DdsService));

  sP->name = strdup(name);
  pthread_mutex_init(&sP->instancesMtx, NULL);

  if (requestType   != NULL) sP->requestType   = strdup(requestType);
  if (requestQoS    != NULL) sP->requestQoS    = strdup(requestQoS);
  if (replyType     != NULL) sP->replyType     = strdup(replyType);
  if (replyQoS      != NULL) sP->replyQoS      = strdup(replyQoS);
  if (entityId      != NULL) sP->entityId      = strdup(entityId);
  if (entityType    != NULL) sP->entityType    = strdup(entityType);
  if (attributeName != NULL) sP->attributeName = strdup(attributeName);

  // Add it to the linked list
  // FIXME: Semaphore!
  sP->next    = ddsServices;
  ddsServices = sP;

  return sP;
}



// -----------------------------------------------------------------------------
//
// ddsServiceInfoAdd -
//
void ddsServiceInfoAdd
(
  DdsService* sP,
  const char* requestType,
  const char* requestQoS,
  const char* replyType,
  const char* replyQoS
)
{
  if (requestType   != NULL) sP->requestType   = strdup(requestType);
  if (requestQoS    != NULL) sP->requestQoS    = strdup(requestQoS);
  if (replyType     != NULL) sP->replyType     = strdup(replyType);
  if (replyQoS      != NULL) sP->replyQoS      = strdup(replyQoS);
}
