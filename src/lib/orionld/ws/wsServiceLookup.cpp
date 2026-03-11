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
#include <string.h>                                              // strcmp

#include "orionld/types/Verb.h"                                  // HTTP_POST, HTTP_PUT, HTTP_PATCH
#include "orionld/serviceRoutines/orionldPostEntities.h"         // orionldPostEntities
#include "orionld/serviceRoutines/orionldPostSubscriptions.h"    // orionldPostSubscriptions
#include "orionld/serviceRoutines/orionldPutEntity.h"            // orionldPutEntity
#include "orionld/serviceRoutines/orionldPatchEntity2.h"         // orionldPatchEntity2
#include "orionld/serviceRoutines/orionldPatchSubscription.h"    // orionldPatchSubscription
#include "orionld/ws/wsSubscriptionPrepare.h"                    // wsSubscriptionPrepare
#include "orionld/ws/wsSubscriptionWire.h"                       // wsSubscriptionWire
#include "orionld/ws/wsServiceLookup.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// wsServiceV - dispatch table for WS operations
//
// Each entry maps a WS operation name to:
//   - The HTTP service routine to call
//   - The HTTP verb (for serviceLookupByServiceRoutine)
//   - Optional pre/post-processing routines
//
static WsService wsServiceV[] =
{
  { "createEntity",        orionldPostEntities,       HTTP_POST,   NULL,                      NULL                },
  { "createSubscription",  orionldPostSubscriptions,  HTTP_POST,   wsSubscriptionPrepare,     wsSubscriptionWire  },
  { "putEntity",           orionldPutEntity,          HTTP_PUT,    NULL,                      NULL                },
  { "patchEntity",         orionldPatchEntity2,       HTTP_PATCH,  NULL,                      NULL                },
  { "patchSubscription",   orionldPatchSubscription,  HTTP_PATCH,  NULL,                      NULL                },
};



// -----------------------------------------------------------------------------
//
// wsServiceLookup - look up a WS service by operation name
//
WsService* wsServiceLookup(const char* operation)
{
  for (unsigned int ix = 0; ix < sizeof(wsServiceV) / sizeof(wsServiceV[0]); ix++)
  {
    if (strcmp(operation, wsServiceV[ix].operation) == 0)
      return &wsServiceV[ix];
  }

  return NULL;
}
