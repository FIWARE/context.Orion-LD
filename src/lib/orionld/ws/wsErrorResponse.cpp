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
#include <stdio.h>                                               // snprintf

#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // wsSend
#include "orionld/ws/wsErrorResponse.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// wsErrorResponse - send an error response back over the WS connection
//
void wsErrorResponse(WsConnection* wsP, int statusCode, const char* title, const char* detail)
{
  char buf[1024];

  snprintf(buf, sizeof(buf),
           "{\"metadata\":{\"statusCode\":%d},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/BadRequestData\","
           "\"title\":\"%s\",\"detail\":\"%s\"}}",
           statusCode, title, detail);

  wsSend(wsP, buf);
}
