#ifndef TEST_FUNCTIONALTEST_FTCLIENT_WSCLIENT_H_
#define TEST_FUNCTIONALTEST_FTCLIENT_WSCLIENT_H_

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
#include "kjson/KjNode.h"                                      // KjNode
}



// -----------------------------------------------------------------------------
//
// postWsConnect - POST /ws/connect
//   Connect to broker WS endpoint, send subscription creation message.
//   Returns the subscription ID as an HTTP response header (WS-Subscription-Id).
//
extern KjNode* postWsConnect(int* statusCodeP);



// -----------------------------------------------------------------------------
//
// wsRouteDispatch - dispatch WS routes with dynamic subscription ID
//   Handles:
//     POST /ws/{subId}/send   - send a JSON message over an existing WS connection
//     GET  /ws/{subId}/dump   - return accumulated WS notifications for this subscription
//     POST /ws/{subId}/close  - close a specific WS connection
//     POST /ws/{subId}/reset  - clear accumulated notifications
//
//   Returns NULL if the URL doesn't match any WS route.
//
extern KjNode* wsRouteDispatch(int* statusCodeP);

#endif  // TEST_FUNCTIONALTEST_FTCLIENT_WSCLIENT_H_
