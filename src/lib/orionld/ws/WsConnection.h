#ifndef SRC_LIB_ORIONLD_WS_WSCONNECTION_H_
#define SRC_LIB_ORIONLD_WS_WSCONNECTION_H_

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
#include <microhttpd.h>                                // MHD_socket, MHD_UpgradeResponseHandle
#include <pthread.h>                                   // pthread_t
#include <microhttpd_ws.h>                             // MHD_WebSocketStream



// -----------------------------------------------------------------------------
//
// WsConnection - tracks a live WebSocket connection and its associated subscription
//
typedef struct WsConnection
{
  MHD_socket                       fd;            // Raw socket for send/recv
  struct MHD_UpgradeResponseHandle* urh;          // For MHD_upgrade_action (close)
  struct MHD_WebSocketStream*       ws;           // Encode/decode stream
  pthread_t                         recvThread;   // Receive loop thread
  char*                             subscriptionId;  // Associated subscription (set after sub creation)
  char*                             tenantName;   // Tenant for this connection (copied from initial request)
  bool                              active;       // Connection is alive
  struct WsConnection*              next;         // Linked list
} WsConnection;

#endif  // SRC_LIB_ORIONLD_WS_WSCONNECTION_H_
