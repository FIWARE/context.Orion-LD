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

#include "orionld/ws/wsCreateSubscription.h"                     // wsCreateSubscription
#include "orionld/ws/wsCreateEntity.h"                           // wsCreateEntity
#include "orionld/ws/wsPutEntity.h"                              // wsPutEntity
#include "orionld/ws/wsPatchSubscription.h"                      // wsPatchSubscription
#include "orionld/ws/wsServiceLookup.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// wsServiceV - dispatch table for WS operations
//
static WsService wsServiceV[] =
{
  { "createSubscription",  wsCreateSubscription },
  { "createEntity",        wsCreateEntity       },
  { "putEntity",           wsPutEntity          },
  { "patchSubscription",   wsPatchSubscription  },
};



// -----------------------------------------------------------------------------
//
// wsServiceLookup - look up a WS service routine by operation name
//
WsServiceRoutine wsServiceLookup(const char* operation)
{
  for (unsigned int ix = 0; ix < sizeof(wsServiceV) / sizeof(wsServiceV[0]); ix++)
  {
    if (strcmp(operation, wsServiceV[ix].operation) == 0)
      return wsServiceV[ix].routine;
  }

  return NULL;
}
