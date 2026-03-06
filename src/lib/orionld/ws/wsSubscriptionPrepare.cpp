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
extern "C"
{
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjChildAdd
#include "kjson/kjNavigate.h"                                    // kjNavigate
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSubscriptionPrepare.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// wsSubscriptionPrepare -
//
// Ensure notification.endpoint.uri exists in the request tree.
// For WS subscriptions, the URI is a placeholder - notifications go over the WS connection.
//
void wsSubscriptionPrepare(WsConnection* wsP)
{
  const char* navPath[] = { "notification", "endpoint", "uri", NULL };
  KjNode*     uriP      = kjNavigate(orionldState.requestTree, navPath, NULL, NULL);

  if (uriP != NULL)
    return;

  KjNode* notifP = kjLookup(orionldState.requestTree, "notification");
  if (notifP == NULL)
  {
    notifP = kjObject(orionldState.kjsonP, "notification");
    kjChildAdd(orionldState.requestTree, notifP);
  }

  KjNode* endpointP = kjLookup(notifP, "endpoint");
  if (endpointP == NULL)
  {
    endpointP = kjObject(orionldState.kjsonP, "endpoint");
    kjChildAdd(notifP, endpointP);
  }

  uriP = kjString(orionldState.kjsonP, "uri", "ws://ws-placeholder:0");
  kjChildAdd(endpointP, uriP);
}
