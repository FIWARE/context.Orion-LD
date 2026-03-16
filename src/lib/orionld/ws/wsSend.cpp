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
#include <string.h>                                              // strlen
#include <sys/socket.h>                                          // send

extern "C"
{
#include <microhttpd.h>                                          // _MHD_EXTERN (must come before microhttpd_ws.h)
#include <microhttpd_ws.h>                                       // MHD_websocket_encode_text, MHD_websocket_free
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // Own interface



// -----------------------------------------------------------------------------
//
// wsSend - send a text message over a WebSocket connection
//
// Encodes the text as a WebSocket text frame and sends it over the raw socket.
// Returns 0 on success, -1 on error.
//
int wsSend(WsConnection* wsP, const char* json)
{
  if ((wsP == NULL) || (wsP->active == false) || (wsP->ws == NULL))
  {
    KT_E("wsSend: invalid WS connection");
    return -1;
  }

  size_t  jsonLen   = strlen(json);
  char*   frame     = NULL;
  size_t  frameLen  = 0;

  int r = MHD_websocket_encode_text(wsP->ws,
                                    json,
                                    jsonLen,
                                    MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                    &frame,
                                    &frameLen,
                                    NULL);  // No UTF-8 step needed
  if (r != MHD_WEBSOCKET_STATUS_OK)
  {
    KT_E("MHD_websocket_encode_text failed: %d", r);
    return -1;
  }

  // Send the frame over the raw socket
  ssize_t  sent    = 0;
  size_t   offset  = 0;

  while (offset < frameLen)
  {
    sent = send((int) wsP->fd, &frame[offset], frameLen - offset, MSG_NOSIGNAL);
    if (sent <= 0)
    {
      KT_E("wsSend: send failed (fd=%d, errno=%d)", (int) wsP->fd, (int) sent);
      MHD_websocket_free(wsP->ws, frame);
      return -1;
    }
    offset += sent;
  }

  MHD_websocket_free(wsP->ws, frame);

  KT_T(StWs, "Sent %zu bytes over WS (fd=%d)", frameLen, (int) wsP->fd);
  return 0;
}
