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
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/mqtt/MqttConnection.h"                       // MqttConnection
#include "orionld/mqtt/mqttConnect.h"                          // mqttConnect
#include "orionld/mqtt/mqttConnectionList.h"                   // Mqtt Connection List
#include "orionld/mqtt/mqttConnectionAdd.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// mqttConnectionAdd -
//
MqttConnection* mqttConnectionAdd(const char* host, unsigned short port)
{
  LM_TMP(("MQTT: Adding connection for MQTT server on '%s', port %d (index %d)", host, port, mqttConnectionListIx));
  if (mqttConnectionListIx >= mqttConnectionListSize)
  {
    mqttConnectionListSize += 20;
    mqttConnectionList      = (MqttConnection*) realloc(mqttConnectionList, sizeof(MqttConnection) * mqttConnectionListSize);
  }

  MqttConnection* mqP = &mqttConnectionList[mqttConnectionListIx];

  mqP->host = (char*) host;
  mqP->port = port;

  if (mqttConnect(mqP, host, port) == false)
    return NULL;

  ++mqttConnectionListIx;

  return mqP;
}
