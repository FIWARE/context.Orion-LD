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
#include <string.h>                                              // strdup
#include <stdlib.h>                                              // free

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSubscriptionWire.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// wsSubscriptionWire - wire a WS connection to the newly created subscription
//
// Called as a WsPostRoutine after orionldPostSubscriptions succeeds.
// The subscription ID may come from:
//   1. orionldState.payloadIdNode (if the client provided an ID in the body)
//   2. The request tree (if the service routine auto-generated the ID and prepended it)
//
void wsSubscriptionWire(WsConnection* wsP)
{
  const char* subscriptionId = NULL;

  // Try payloadIdNode first (client-provided ID)
  if (orionldState.payloadIdNode != NULL)
    subscriptionId = orionldState.payloadIdNode->value.s;

  // If not there, the service routine generated it and prepended to requestTree
  if (subscriptionId == NULL && orionldState.requestTree != NULL)
  {
    KjNode* idP = kjLookup(orionldState.requestTree, "id");
    if (idP == NULL)
      idP = kjLookup(orionldState.requestTree, "_id");
    if (idP != NULL && idP->type == KjString)
      subscriptionId = idP->value.s;
  }

  if (subscriptionId == NULL)
  {
    KT_W("Cannot wire WS connection - no subscription ID found (payloadIdNode=%p, requestTree=%p)",
         orionldState.payloadIdNode, orionldState.requestTree);
    return;
  }

  //
  // The connection remembers its subscription, and that is the whole wiring:
  // wsNotify looks the connection up BY SUBSCRIPTION ID, so that a closed
  // connection can never be handed back, and the subscription's own endpoint URI
  // ("urn:ngsi-ld:ws:<fd>") is what tells the sub cache the protocol is WS.
  //
  free(wsP->subscriptionId);
  wsP->subscriptionId = strdup(subscriptionId);

  KT_T(StWs, "Subscription '%s' wired to WS connection (fd=%d)", subscriptionId, (int) wsP->fd);
}
