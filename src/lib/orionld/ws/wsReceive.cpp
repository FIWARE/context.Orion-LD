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
#include <string.h>                                              // memset
#include <stdlib.h>                                              // free
#include <unistd.h>                                              // close
#include <errno.h>                                               // errno
#include <poll.h>                                                // poll, struct pollfd
#include <sys/socket.h>                                          // recv, send

extern "C"
{
#include <microhttpd.h>                                          // MHD_upgrade_action
#include <microhttpd_ws.h>                                       // MHD_websocket_decode, MHD_websocket_encode_pong, MHD_websocket_encode_close, MHD_websocket_free, MHD_websocket_stream_free
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsConnectionList.h"                         // wsConnectionRemove
#include "orionld/ws/wsMessageDispatch.h"                        // wsMessageDispatch
#include "orionld/ws/wsClose.h"                                  // wsClose
#include "orionld/ws/wsReceive.h"                                // Own interface



// -----------------------------------------------------------------------------
//
// wsReceiveLoop - receive loop for a WebSocket connection (runs in its own thread)
//
// Reads data from the raw socket, decodes WebSocket frames via MHD_websocket_decode,
// and dispatches text frames to wsMessageDispatch.
// Handles ping (auto-pong), close, and disconnects.
//
void* wsReceiveLoop(void* arg)
{
  WsConnection* wsP = (WsConnection*) arg;
  char          buf[4096];

  KT_T(StWs, "WS receive loop started (fd=%d)", (int) wsP->fd);

  struct pollfd pfd;
  pfd.fd     = (int) wsP->fd;
  pfd.events = POLLIN;

  while (wsP->active)
  {
    int pollRet = poll(&pfd, 1, 1000);  // 1 second timeout

    if (pollRet < 0)
    {
      if (errno == EINTR)
        continue;
      KT_T(StWs, "WS poll error (fd=%d, errno=%d: %s) - closing", (int) wsP->fd, errno, strerror(errno));
      break;
    }

    if (pollRet == 0)
      continue;  // Timeout, check wsP->active and loop

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
    {
      KT_T(StWs, "WS poll error event (fd=%d, revents=0x%x) - closing", (int) wsP->fd, pfd.revents);
      break;
    }

    ssize_t bytesRead = recv((int) wsP->fd, buf, sizeof(buf), 0);

    if (bytesRead < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue;

      KT_T(StWs, "WS recv error (fd=%d, errno=%d: %s) - closing", (int) wsP->fd, errno, strerror(errno));
      break;
    }

    if (bytesRead == 0)
    {
      KT_T(StWs, "WS connection closed by peer (fd=%d)", (int) wsP->fd);
      break;
    }

    // Decode WebSocket frames
    size_t  bufOffset = 0;

    while (bufOffset < (size_t) bytesRead)
    {
      size_t  newOffset = 0;
      char*   payload   = NULL;
      size_t  payloadLen = 0;

      int status = MHD_websocket_decode(wsP->ws,
                                        &buf[bufOffset],
                                        (size_t) bytesRead - bufOffset,
                                        &newOffset,
                                        &payload,
                                        &payloadLen);
      bufOffset += newOffset;

      if (status == MHD_WEBSOCKET_STATUS_OK)
      {
        // Need more data
        break;
      }
      else if (status == MHD_WEBSOCKET_STATUS_TEXT_FRAME)
      {
        // Text frame received - dispatch the message
        KT_T(StWs, "WS text frame received (%zu bytes, fd=%d)", payloadLen, (int) wsP->fd);
        wsMessageDispatch(wsP, payload, payloadLen);
        MHD_websocket_free(wsP->ws, payload);
      }
      else if (status == MHD_WEBSOCKET_STATUS_PING_FRAME)
      {
        // Reply with pong
        char*   pong    = NULL;
        size_t  pongLen = 0;

        int r = MHD_websocket_encode_pong(wsP->ws, payload, payloadLen, &pong, &pongLen);
        if (r == MHD_WEBSOCKET_STATUS_OK)
        {
          send((int) wsP->fd, pong, pongLen, MSG_NOSIGNAL);
          MHD_websocket_free(wsP->ws, pong);
        }
        MHD_websocket_free(wsP->ws, payload);
      }
      else if (status == MHD_WEBSOCKET_STATUS_CLOSE_FRAME)
      {
        // Client initiated close
        KT_T(StWs, "WS close frame received (fd=%d)", (int) wsP->fd);

        // Send close frame back
        char*   closeFrame    = NULL;
        size_t  closeFrameLen = 0;

        int r = MHD_websocket_encode_close(wsP->ws,
                                           MHD_WEBSOCKET_CLOSEREASON_REGULAR,
                                           NULL, 0,
                                           &closeFrame, &closeFrameLen);
        if (r == MHD_WEBSOCKET_STATUS_OK)
        {
          send((int) wsP->fd, closeFrame, closeFrameLen, MSG_NOSIGNAL);
          MHD_websocket_free(wsP->ws, closeFrame);
        }
        MHD_websocket_free(wsP->ws, payload);

        goto done;
      }
      else if (status == MHD_WEBSOCKET_STATUS_PONG_FRAME)
      {
        // Pong - just ignore
        MHD_websocket_free(wsP->ws, payload);
      }
      else
      {
        // Protocol error or other issue
        KT_W("WS decode returned unexpected status: %d (fd=%d)", status, (int) wsP->fd);
        if (payload != NULL)
          MHD_websocket_free(wsP->ws, payload);

        // If we got an error frame, send it and close
        if (status < 0)
          goto done;

        break;
      }
    }
  }

done:
  // Close the WS connection and PAUSE associated subscription
  wsP->active = false;
  wsClose(wsP);

  return NULL;
}
