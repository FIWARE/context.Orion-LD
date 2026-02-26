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
#include <string.h>                                              // strlen, strdup
#include <stdlib.h>                                              // malloc, free
#include <microhttpd.h>                                          // MHD

extern "C"
{
#include <microhttpd_ws.h>                                       // MHD_websocket_*
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsReceive.h"                                // wsReceiveLoop
#include "orionld/ws/wsConnectionList.h"                         // wsConnectionAdd, wsConnectionRemove
#include "orionld/ws/wsUpgrade.h"                                // Own interface



// -----------------------------------------------------------------------------
//
// wsUpgradeCallback - MHD upgrade callback, called after 101 is sent
//
// With MHD_USE_THREAD_PER_CONNECTION, this callback runs in the connection's
// dedicated MHD thread.  It MUST block until the WS connection is done,
// because returning early causes MHD to tear down the connection resources
// while the socket is still in use, leading to a crash.
//
// The receive loop runs directly here (no separate thread needed).
// When the loop exits (close frame or disconnect), wsClose() calls
// MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE) before we return.
//
static void wsUpgradeCallback
(
  void*                             cls,
  struct MHD_Connection*            connection,
  void*                             req_cls,
  const char*                       extra_in,
  size_t                            extra_in_size,
  MHD_socket                        sock,
  struct MHD_UpgradeResponseHandle* urh
)
{
  (void) connection;
  (void) req_cls;
  (void) extra_in;
  (void) extra_in_size;

  WsConnection* wsP = (WsConnection*) cls;

  wsP->fd  = sock;
  wsP->urh = urh;

  // Initialize the WebSocket stream (server mode, no fragmentation)
  int r = MHD_websocket_stream_init(&wsP->ws,
                                    MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                    0);  // 0 = unlimited payload size
  if (r != MHD_WEBSOCKET_STATUS_OK)
  {
    KT_E("MHD_websocket_stream_init failed: %d", r);
    MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
    free(wsP);
    return;
  }

  wsP->active = true;

  // Add to the global WS connection list
  wsConnectionAdd(wsP);

  KT_T(StWs, "WebSocket connection upgraded successfully (fd=%d)", (int) sock);

  // Run the receive loop directly in this thread (blocks until WS connection closes)
  // wsReceiveLoop calls wsClose() which calls MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE)
  wsReceiveLoop(wsP);

  //
  // After the WS receive loop exits, MHD will call requestCompleted() for the
  // original upgrade connection.  requestCompleted calls kaBufferReset() which
  // walks allocList and frees each block.  But wsRequestCleanup() already freed
  // those blocks during the last WS message.  NULL out allocList so MHD's
  // kaBufferReset finds nothing to double-free.
  //
  orionldState.kalloc.allocList     = NULL;
  orionldState.kalloc.allocListTail = NULL;
}



// -----------------------------------------------------------------------------
//
// wsUpgrade - handle WebSocket upgrade request
//
// Called from mhdConnectionTreat when wsUpgrade flag is set.
// Validates the WS handshake headers, creates the accept key, and queues
// a 101 Switching Protocols response with the upgrade callback.
//
MHD_Result wsUpgrade(MHD_Connection* connection, const char* wsKey)
{
  if (wsKey == NULL)
  {
    KT_E("WebSocket upgrade request without Sec-WebSocket-Key");
    return MHD_NO;
  }

  // Create the Sec-WebSocket-Accept value
  char acceptHeader[29];  // MHD_websocket_create_accept_header needs at least 29 bytes
  int  r = MHD_websocket_create_accept_header(wsKey, acceptHeader);

  if (r != MHD_WEBSOCKET_STATUS_OK)
  {
    KT_E("MHD_websocket_create_accept_header failed: %d", r);
    return MHD_NO;
  }

  // Allocate the WsConnection struct (freed by the receive thread or upgrade callback on error)
  WsConnection* wsP = (WsConnection*) malloc(sizeof(WsConnection));
  if (wsP == NULL)
  {
    KT_E("Out of memory allocating WsConnection");
    return MHD_NO;
  }
  memset(wsP, 0, sizeof(WsConnection));

  // Copy tenant name if present (it lives in the MHD thread and will be freed)
  if (orionldState.tenantName != NULL)
    wsP->tenantName = strdup(orionldState.tenantName);

  // Create upgrade response
  MHD_Response* response = MHD_create_response_for_upgrade(wsUpgradeCallback, wsP);
  if (response == NULL)
  {
    KT_E("MHD_create_response_for_upgrade failed");
    free(wsP->tenantName);
    free(wsP);
    return MHD_NO;
  }

  // Add required WebSocket response headers
  MHD_add_response_header(response, "Sec-WebSocket-Accept", acceptHeader);
  MHD_add_response_header(response, "Upgrade", "websocket");

  // Queue the 101 Switching Protocols response
  MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_SWITCHING_PROTOCOLS, response);
  MHD_destroy_response(response);

  KT_I("------------------------- WebSocket upgrade from %s, tenant: %s -------------------------",
        orionldState.clientIp, wsP->tenantName ? wsP->tenantName : "default");
  KT_T(StWs, "WebSocket upgrade response queued (ret=%d)", ret);

  return ret;
}
