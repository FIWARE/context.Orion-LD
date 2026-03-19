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
#include <string.h>                                            // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/types/MqttConnection.h"                      // MqttConnection
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/mqtt/mqttConnectionList.h"                   // Mqtt Connection List
#include "orionld/mqtt/mqttConnectionLookup.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// mqttConnectionLookup -
//
MqttConnection* mqttConnectionLookup(bool mqtts, const char* host, unsigned short port, const char* username, const char* password, const char* version)
{
  KT_T(KtMqtt, "mqttConnectionListIx == %d", mqttConnectionListIx);

  if (host == NULL) return NULL;

  KT_T(KtMqtt, "Looking up an MQTT%s connection for %s:%d (user: '%s', pwd: '%s', ver: '%s')", mqtts ? "S" : "", host, port, username, password, version);

  for (int ix = 0; ix < mqttConnectionListIx; ix++)
  {
    MqttConnection* mqP = &mqttConnectionList[ix];

    if (mqP->host == NULL)                                                  continue;  // Free slot - no match

    if (mqP->mqtts != mqtts)                                                continue;
    if (mqP->port != port)                                                  continue;
    if (strcmp(host, mqP->host) != 0)                                       continue;  // Host is mandatory, cannot be empty

    KT_T(KtMqtt, "Comparing with MQTT connection %s:%d (user: '%s', pwd: '%s', ver: '%s')", mqP->host, mqP->port, mqP->username, mqP->password, mqP->version);

    if (((username == NULL) || (*username == 0)) && (mqP->username == NULL))
      {}  // Match
    else if ((username != NULL) && (mqP->username != NULL) && strcmp(username, mqP->username) == 0)
      {}  // Match
    else
      continue;

    if (((password == NULL) || (*password == 0)) && (mqP->password == NULL))
      {}  // Match
    else if ((password != NULL) && (mqP->password != NULL) && strcmp(password, mqP->password) == 0)
      {}  // Match
    else
      continue;

    if ((version == NULL) || (*version == 0))
      {}  // Match
    else if (mqP->version == NULL)
      {}  // Match
    else if ((version != NULL) && (mqP->version != NULL) && strcmp(version, mqP->version) == 0)
      {}  // Match
    else
      continue;

    if (MQTTClient_isConnected(mqP->client) != true)
      KT_T(KtMqtt, "Found the MQTT connection, just, it's not connected!");
    else
      KT_T(KtMqtt, "Found the MQTT connection and it's connected");

    return mqP;
  }

  KT_T(KtMqtt, "No MQTT connection found for %s:%d (user: '%s', pwd: '%s', ver: '%s')", host, port, username, password, version);

  return NULL;
}
