#ifndef TEST_FUNCTIONALTEST_FTCLIENT_MQTTCLIENT_H_
#define TEST_FUNCTIONALTEST_FTCLIENT_MQTTCLIENT_H_

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
// postMqttSub - POST /mqtt/sub
//   Subscribe to an MQTT broker topic.
//   Body: { "host": "...", "port": 1883, "topic": "...", "mqtts": false }
//
extern KjNode* postMqttSub(int* statusCodeP);



// -----------------------------------------------------------------------------
//
// mqttRouteDispatch - dispatch MQTT routes with dynamic topic
//   Handles:
//     GET    /mqtt/{topic}/dump   - return accumulated MQTT notifications
//     DELETE /mqtt/{topic}/dump   - reset accumulated notifications
//     POST   /mqtt/{topic}/unsub  - unsubscribe and disconnect
//
//   Returns NULL if the URL doesn't match any MQTT route.
//
extern KjNode* mqttRouteDispatch(int* statusCodeP);

#endif  // TEST_FUNCTIONALTEST_FTCLIENT_MQTTCLIENT_H_
