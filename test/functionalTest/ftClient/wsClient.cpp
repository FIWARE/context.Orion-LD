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
#include <stdio.h>                                             // snprintf
#include <string.h>                                            // strlen, strcmp, strncmp, strdup, strchr
#include <stdlib.h>                                            // malloc, free
#include <unistd.h>                                            // close
#include <sys/socket.h>                                        // socket, connect, send, recv
#include <netinet/in.h>                                        // sockaddr_in
#include <arpa/inet.h>                                         // inet_pton
#include <pthread.h>                                           // pthread_create, pthread_detach
#include <netdb.h>                                             // gethostbyname

extern "C"
{
#include <microhttpd.h>                                        // _MHD_EXTERN (must come before microhttpd_ws.h)
#include <microhttpd_ws.h>                                     // MHD_websocket_*
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, kjArray, kjChildAdd
#include "kjson/kjRender.h"                                    // kjRender
#include "kjson/kjRenderSize.h"                                // kjRenderSize
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjClone.h"                                     // kjClone
}

#include "common/orionldState.h"                               // orionldState
#include "common/traceLevels.h"                                // Trace levels
#include "ftClient/wsClient.h"                                 // Own interface



// -----------------------------------------------------------------------------
//
// WsClientConnection - tracks a WS connection from ftClient to the broker
//
typedef struct WsClientConnection
{
  char*                             subscriptionId;   // Keyed by subscription ID
  int                               fd;               // Raw socket to the broker
  struct MHD_WebSocketStream*       ws;               // Client-mode WS stream
  pthread_t                         recvThread;       // Thread accumulating notifications
  KjNode*                           notifications;    // Array of accumulated notifications (KjNode)
  pthread_mutex_t                   mutex;            // Protects notifications array
  bool                              active;
  struct WsClientConnection*        next;
} WsClientConnection;

static WsClientConnection*  wsClientList     = NULL;
static pthread_mutex_t      wsClientListMutex = PTHREAD_MUTEX_INITIALIZER;



// -----------------------------------------------------------------------------
//
// wsClientLookup - find a client connection by subscription ID
//
static WsClientConnection* wsClientLookup(const char* subId)
{
  pthread_mutex_lock(&wsClientListMutex);

  WsClientConnection* p = wsClientList;
  while (p != NULL)
  {
    if ((p->subscriptionId != NULL) && (strcmp(p->subscriptionId, subId) == 0))
    {
      pthread_mutex_unlock(&wsClientListMutex);
      return p;
    }
    p = p->next;
  }

  pthread_mutex_unlock(&wsClientListMutex);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// wsClientAdd - add to the global list
//
static void wsClientAdd(WsClientConnection* wscP)
{
  pthread_mutex_lock(&wsClientListMutex);
  wscP->next   = wsClientList;
  wsClientList  = wscP;
  pthread_mutex_unlock(&wsClientListMutex);
}



// -----------------------------------------------------------------------------
//
// wsClientRecvLoop - thread function to accumulate WS notifications from broker
//
static void* wsClientRecvLoop(void* arg)
{
  WsClientConnection* wscP = (WsClientConnection*) arg;
  char                buf[8192];

  while (wscP->active)
  {
    ssize_t bytesRead = recv(wscP->fd, buf, sizeof(buf), 0);
    if (bytesRead <= 0)
      break;

    size_t bufOffset = 0;
    while (bufOffset < (size_t) bytesRead)
    {
      size_t  newOffset  = 0;
      char*   payload    = NULL;
      size_t  payloadLen = 0;

      int status = MHD_websocket_decode(wscP->ws,
                                        &buf[bufOffset],
                                        (size_t) bytesRead - bufOffset,
                                        &newOffset,
                                        &payload,
                                        &payloadLen);
      bufOffset += newOffset;

      if (status == MHD_WEBSOCKET_STATUS_OK)
        break;  // Need more data

      if (status == MHD_WEBSOCKET_STATUS_TEXT_FRAME)
      {
        // Parse JSON and accumulate as structured object (like DDS does)
        orionldStateInit(NULL);
        char* copy = strdup(payload);
        KjNode* notif = kjParse(orionldState.kjsonP, copy);

        if (notif != NULL)
        {
          notif = kjClone(NULL, notif);

          pthread_mutex_lock(&wscP->mutex);
          if (wscP->notifications == NULL)
            wscP->notifications = kjArray(NULL, NULL);
          kjChildAdd(wscP->notifications, notif);
          pthread_mutex_unlock(&wscP->mutex);
        }

        free(copy);
        MHD_websocket_free(wscP->ws, payload);
      }
      else if (status == MHD_WEBSOCKET_STATUS_CLOSE_FRAME)
      {
        MHD_websocket_free(wscP->ws, payload);
        goto done;
      }
      else if (status == MHD_WEBSOCKET_STATUS_PING_FRAME)
      {
        // Auto-pong
        char*   pong    = NULL;
        size_t  pongLen = 0;
        int r = MHD_websocket_encode_pong(wscP->ws, payload, payloadLen, &pong, &pongLen);
        if (r == MHD_WEBSOCKET_STATUS_OK)
        {
          send(wscP->fd, pong, pongLen, MSG_NOSIGNAL);
          MHD_websocket_free(wscP->ws, pong);
        }
        MHD_websocket_free(wscP->ws, payload);
      }
      else
      {
        if (payload != NULL)
          MHD_websocket_free(wscP->ws, payload);
        if (status < 0)
          goto done;
        break;
      }
    }
  }

done:
  wscP->active = false;
  return NULL;
}



// -----------------------------------------------------------------------------
//
// rng callback for MHD_websocket_stream_init2 (client mode needs random masking)
//
static size_t wsClientRng(void* cls, void* buf, size_t bufLen)
{
  (void) cls;
  // Simple random fill for masking
  unsigned char* p = (unsigned char*) buf;
  for (size_t i = 0; i < bufLen; i++)
    p[i] = (unsigned char) (rand() & 0xFF);
  return bufLen;
}



// -----------------------------------------------------------------------------
//
// postWsConnect - POST /ws/connect
//
// Connects to the broker's WS endpoint, performs HTTP upgrade handshake,
// sends the subscription creation message from the POST body.
// Reads back the broker's response and extracts the subscription ID.
//
// The POST body should be the full WS message envelope:
//   {"metadata": {..., "operation": "createSubscription"}, "body": {subscription payload}}
//
// The broker host/port is taken from query params or defaults to localhost:CB_PORT.
//
extern __thread KjNode* uriParams;

KjNode* postWsConnect(int* statusCodeP)
{
  const char* host = "localhost";
  int         port = 9999;  // Default; CB_PORT

  // Extract brokerPort from URI params if present (e.g., /ws/connect?brokerPort=9999)
  if (uriParams != NULL)
  {
    KjNode* portP = kjLookup(uriParams, "brokerPort");
    if (portP != NULL && portP->type == KjString)
      port = atoi(portP->value.s);
  }

  // Create TCP socket and connect to broker
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    *statusCodeP = 500;
    return kjString(orionldState.kjsonP, "error", "Cannot create socket");
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (connect(fd, (struct sockaddr*) &addr, sizeof(addr)) < 0)
  {
    close(fd);
    *statusCodeP = 502;
    return kjString(orionldState.kjsonP, "error", "Cannot connect to broker");
  }

  //
  // Perform HTTP WebSocket upgrade handshake
  //
  const char* wsKey = "dGhlIHNhbXBsZSBub25jZQ==";  // Standard test key (base64 of "the sample nonce")

  char upgradeRequest[1024];
  int  reqLen = snprintf(upgradeRequest, sizeof(upgradeRequest),
    "GET /ws HTTP/1.1\r\n"
    "Host: %s:%d\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: %s\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n",
    host, port, wsKey);

  send(fd, upgradeRequest, reqLen, 0);

  // Read upgrade response
  char response[4096];
  ssize_t respLen = recv(fd, response, sizeof(response) - 1, 0);
  if (respLen <= 0)
  {
    close(fd);
    *statusCodeP = 502;
    return kjString(orionldState.kjsonP, "error", "No upgrade response from broker");
  }
  response[respLen] = 0;

  // Verify 101 Switching Protocols
  if (strstr(response, "101") == NULL)
  {
    close(fd);
    *statusCodeP = 502;
    KjNode* errP = kjObject(orionldState.kjsonP, NULL);
    kjChildAdd(errP, kjString(orionldState.kjsonP, "error", "Broker did not accept WebSocket upgrade"));
    kjChildAdd(errP, kjString(orionldState.kjsonP, "response", response));
    return errP;
  }

  // Initialize WS stream in CLIENT mode
  WsClientConnection* wscP = (WsClientConnection*) malloc(sizeof(WsClientConnection));
  memset(wscP, 0, sizeof(WsClientConnection));
  wscP->fd     = fd;
  wscP->active = true;
  pthread_mutex_init(&wscP->mutex, NULL);

  int r = MHD_websocket_stream_init2(&wscP->ws,
                                     MHD_WEBSOCKET_FLAG_CLIENT | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                     0,
                                     malloc, realloc, free,
                                     NULL, wsClientRng);
  if (r != MHD_WEBSOCKET_STATUS_OK)
  {
    close(fd);
    free(wscP);
    *statusCodeP = 500;
    return kjString(orionldState.kjsonP, "error", "Failed to init WS client stream");
  }

  //
  // Send the subscription creation message (from POST body)
  //
  if (orionldState.requestTree != NULL)
  {
    int   bodyBufSize = kjRenderSize(orionldState.kjsonP, orionldState.requestTree);
    char* bodyBuf     = (char*) malloc(bodyBufSize);
    kjRender(orionldState.kjsonP, orionldState.requestTree, bodyBuf);

    // Encode as WS text frame
    char*   frame    = NULL;
    size_t  frameLen = 0;
    r = MHD_websocket_encode_text(wscP->ws, bodyBuf, strlen(bodyBuf),
                                  MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                  &frame, &frameLen, NULL);
    if (r == MHD_WEBSOCKET_STATUS_OK)
    {
      send(fd, frame, frameLen, MSG_NOSIGNAL);
      MHD_websocket_free(wscP->ws, frame);
    }
    free(bodyBuf);
  }

  //
  // Read back the broker's response to get the subscription ID
  // Wait briefly for the response
  //
  char respBuf[4096];
  respLen = recv(fd, respBuf, sizeof(respBuf) - 1, 0);

  char* subId = NULL;
  if (respLen > 0)
  {
    respBuf[respLen] = 0;

    // Decode WS frame
    size_t  newOffset  = 0;
    char*   payload    = NULL;
    size_t  payloadLen = 0;

    int status = MHD_websocket_decode(wscP->ws, respBuf, respLen, &newOffset, &payload, &payloadLen);
    if (status == MHD_WEBSOCKET_STATUS_TEXT_FRAME && payload != NULL)
    {
      //
      // Parse the response to extract subscription ID
      // Expected: {"metadata":{"statusCode":201,"subscriptionId":"urn:..."}, "body":{"id":"urn:...","status":"created"}}
      //
      char* copy = strdup(payload);
      KjNode* respTree = kjParse(orionldState.kjsonP, copy);

      if (respTree != NULL)
      {
        KjNode* metadataP = kjLookup(respTree, "metadata");
        if (metadataP != NULL)
        {
          KjNode* subIdP = kjLookup(metadataP, "subscriptionId");
          if (subIdP != NULL && subIdP->type == KjString)
            subId = strdup(subIdP->value.s);
        }
      }

      free(copy);
      MHD_websocket_free(wscP->ws, payload);
    }
  }

  if (subId == NULL)
  {
    close(fd);
    MHD_websocket_stream_free(wscP->ws);
    free(wscP);
    *statusCodeP = 502;
    return kjString(orionldState.kjsonP, "error", "No subscription ID in broker WS response");
  }

  wscP->subscriptionId = subId;

  // Start the receive thread to accumulate notifications
  pthread_t pt;
  if (pthread_create(&pt, NULL, wsClientRecvLoop, wscP) == 0)
  {
    wscP->recvThread = pt;
    pthread_detach(pt);
  }

  // Add to global list
  wsClientAdd(wscP);

  // Return subscription ID
  *statusCodeP = 201;
  KjNode* result = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(result, kjString(orionldState.kjsonP, "subscriptionId", subId));

  return result;
}



// -----------------------------------------------------------------------------
//
// wsRouteDispatch - dispatch WS routes with dynamic subscription ID
//
// URL pattern: /ws/{subId}/{action}
// Actions: send, dump, close, reset
//
KjNode* wsRouteDispatch(int* statusCodeP)
{
  // URL: /ws/{subId}/{action}
  // orionldState.urlPath starts with "/ws/"
  char* path = orionldState.urlPath;

  if (strncmp(path, "/ws/", 4) != 0)
    return NULL;  // Not a WS route

  // Extract subId and action
  char* subIdStart = &path[4];
  char* slash      = strchr(subIdStart, '/');
  if (slash == NULL)
    return NULL;

  // Zero-terminate subId
  *slash = 0;
  char* subId  = subIdStart;
  char* action = &slash[1];

  // Restore for logging
  KT_T(StWs, "WS route: subId='%s', action='%s'", subId, action);

  WsClientConnection* wscP = wsClientLookup(subId);

  if (strcmp(action, "dump") == 0)
  {
    // GET /ws/{subId}/dump - return accumulated notifications
    *statusCodeP = 200;

    if (wscP == NULL)
      return kjArray(orionldState.kjsonP, NULL);

    pthread_mutex_lock(&wscP->mutex);
    KjNode* result = (wscP->notifications != NULL) ?
                     kjClone(orionldState.kjsonP, wscP->notifications) :
                     kjArray(orionldState.kjsonP, NULL);
    pthread_mutex_unlock(&wscP->mutex);

    return result;
  }
  else if (strcmp(action, "send") == 0)
  {
    // POST /ws/{subId}/send - send a message over an existing WS connection
    if (wscP == NULL || wscP->active == false)
    {
      *statusCodeP = 404;
      return kjString(orionldState.kjsonP, "error", "WS connection not found or inactive");
    }

    if (orionldState.requestTree != NULL)
    {
      int   bufSize = kjRenderSize(orionldState.kjsonP, orionldState.requestTree);
      char* buf     = (char*) malloc(bufSize);
      kjRender(orionldState.kjsonP, orionldState.requestTree, buf);

      char*   frame    = NULL;
      size_t  frameLen = 0;
      int r = MHD_websocket_encode_text(wscP->ws, buf, strlen(buf),
                                        MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                        &frame, &frameLen, NULL);
      if (r == MHD_WEBSOCKET_STATUS_OK)
      {
        send(wscP->fd, frame, frameLen, MSG_NOSIGNAL);
        MHD_websocket_free(wscP->ws, frame);
      }
      free(buf);
    }

    *statusCodeP = 204;
    return NULL;
  }
  else if (strcmp(action, "close") == 0)
  {
    // POST /ws/{subId}/close - close a specific WS connection
    if (wscP == NULL)
    {
      *statusCodeP = 404;
      return kjString(orionldState.kjsonP, "error", "WS connection not found");
    }

    // Send close frame
    char*   closeFrame    = NULL;
    size_t  closeFrameLen = 0;
    int r = MHD_websocket_encode_close(wscP->ws,
                                       MHD_WEBSOCKET_CLOSEREASON_REGULAR,
                                       NULL, 0,
                                       &closeFrame, &closeFrameLen);
    if (r == MHD_WEBSOCKET_STATUS_OK)
    {
      send(wscP->fd, closeFrame, closeFrameLen, MSG_NOSIGNAL);
      MHD_websocket_free(wscP->ws, closeFrame);
    }

    wscP->active = false;
    close(wscP->fd);

    *statusCodeP = 204;
    return NULL;
  }
  else if (strcmp(action, "reset") == 0)
  {
    // POST /ws/{subId}/reset - clear accumulated notifications
    if (wscP == NULL)
    {
      *statusCodeP = 404;
      return kjString(orionldState.kjsonP, "error", "WS connection not found");
    }

    pthread_mutex_lock(&wscP->mutex);
    // Free old notifications array
    // Note: kjFree would be ideal but the nodes were allocated with different allocators
    wscP->notifications = NULL;
    pthread_mutex_unlock(&wscP->mutex);

    *statusCodeP = 204;
    return NULL;
  }

  *statusCodeP = 400;
  return kjString(orionldState.kjsonP, "error", "Unknown WS action");
}
