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
#include <stdlib.h>                                         // malloc
#include <string.h>                                         // strdup
#include <strings.h>                                        // bzero

#include "orionld/types/DdsAction.h"                        // DdsAction
#include "orionld/common/orionldState.h"                    // ddsActions
#include "orionld/dds/ddsActionCreate.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionCreate -
//
DdsAction* ddsActionCreate
(
  const char* name,
  const char* entityId,
  const char* entityType,
  const char* attributeName
)
{
  DdsAction* aP = (DdsAction*) malloc(sizeof(DdsAction));

  bzero(aP, sizeof(DdsAction));

  aP->name = strdup(name);
  if (entityId      != NULL) aP->entityId      = strdup(entityId);
  if (entityType    != NULL) aP->entityType    = strdup(entityType);
  if (attributeName != NULL) aP->attributeName = strdup(attributeName);

  // Add to linked list
  aP->next   = ddsActions;
  ddsActions = aP;

  return aP;
}
