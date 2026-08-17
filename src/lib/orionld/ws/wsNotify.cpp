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
#include <string.h>                                            // strlen, strncpy, strchr
#include <sys/uio.h>                                           // struct iovec

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjRenderSize.h"                                // kjFastRenderSize
#include "kjson/kjRender.h"                                    // kjFastRender
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, kjChildAdd
#include "kalloc/kaAlloc.h"                                    // kaAlloc
}

#include "orionld/types/SubCacheItem.h"                         // SubCacheItem

#include "orionld/common/orionldState.h"                       // orionldState, coreContextUrl
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/notifications/notificationSuccess.h"         // notificationSuccess
#include "orionld/notifications/notificationFailure.h"         // notificationFailure
#include "orionld/ws/WsConnection.h"                           // WsConnection
#include "orionld/ws/wsConnectionList.h"                       // wsConnectionLookup
#include "orionld/ws/wsSend.h"                                 // wsSend
#include "orionld/ws/wsNotify.h"                               // Own interface



// -----------------------------------------------------------------------------
//
// headersParse - parse HTTP-style headers from iovec into a metadata JSON object
//
// Same pattern as mqttNotify.cpp:headersParse.
// Extracts headers from the iovec array (skipping the first line which is the request line
// and the last two which are the blank line delimiter + payload body).
//
static KjNode* headersParse(struct iovec* ioVec, int ioVecSize, SubCacheItem* cSubP)
{
  KjNode* metadata = kjObject(orionldState.kjsonP, NULL);

  //
  // Skip the first line (HTTP request line) and iterate headers
  // The last 2 slots are: empty separator ("\r\n") + payload body
  //
  for (int ix = 1; ix < ioVecSize - 2; ix++)
  {
    char* headerReadOnly = (char*) ioVec[ix].iov_base;
    int   headerLen      = ioVec[ix].iov_len;
    char  header[256];

    if (strncmp(headerReadOnly, "Content-Length:", 15) == 0)
      continue;

    if (strncmp(headerReadOnly, "Link:", 5) == 0)
    {
      const char* link = ((cSubP->contextP != NULL) && (cSubP->contextP->url != NULL))? cSubP->contextP->url : coreContextUrl;
      KjNode* linkNodeP = kjString(orionldState.kjsonP, "Link", link);
      kjChildAdd(metadata, linkNodeP);
      continue;
    }

    strncpy(header, headerReadOnly, sizeof(header) - 1);
    header[sizeof(header) - 1] = 0;

    if (header[0] == '\r')
      break;

    // Remove trailing \r\n
    if (headerLen >= 2)
      header[headerLen - 2] = 0;

    char* colonP = strchr(header, ':');
    if (colonP == NULL)
      continue;

    *colonP = 0;
    char* key   = header;
    char* value = &colonP[1];

    while (*value == ' ')
      ++value;

    KjNode* headerNodeP = kjString(orionldState.kjsonP, key, value);
    kjChildAdd(metadata, headerNodeP);
  }

  return metadata;
}



// -----------------------------------------------------------------------------
//
// wsNotify - send an NGSI-LD notification over a WebSocket connection
//
// Pattern follows mqttNotify():
//   1. Parse iovec headers into a "metadata" JSON object
//   2. Get the body from the last iovec slot
//   3. Build JSON envelope: {"metadata": {...}, "body": {...}}
//   4. Send via wsSend() using the subscription's wsConnectionP
//   5. Call notificationSuccess()/notificationFailure() for stats
//
int wsNotify(SubCacheItem* cSubP, struct iovec* ioVec, int ioVecSize, double notificationTime)
{
  //
  // Find the WS connection of the subscription. The connection is owned by the WS
  // layer, not by the subscription cache - looking it up by subscription id is the
  // only way that cannot hand back a connection that has since been closed.
  //
  WsConnection* wsP = wsConnectionLookup(cSubP->subId);

  if (wsP == NULL)
  {
    KT_W("wsNotify: no WS connection for subscription '%s'", cSubP->subId);
    notificationFailure(cSubP, "No WebSocket connection", notificationTime);
    return -1;
  }

  if (wsP->active == false)
  {
    KT_W("wsNotify: WS connection not active for subscription '%s'", cSubP->subId);
    notificationFailure(cSubP, "WebSocket connection not active", notificationTime);
    return -1;
  }

  // Parse headers into metadata
  KjNode* metadata = headersParse(ioVec, ioVecSize, cSubP);
  if (metadata == NULL)
  {
    notificationFailure(cSubP, "Error parsing notification headers", notificationTime);
    return -1;
  }

  int   headersLen = kjFastRenderSize(metadata);
  int   bodyLen    = ioVec[ioVecSize - 1].iov_len;
  int   totalLen   = headersLen + bodyLen + 32;  // Extra for {"metadata":...,"body":...}
  char* buf        = (char*) kaAlloc(&orionldState.kalloc, totalLen);

  if (buf == NULL)
  {
    notificationFailure(cSubP, "Out of memory", notificationTime);
    return -1;
  }

  // Build: {"metadata": <rendered-headers>, "body": <payload>}
  strcpy(buf, "{\"metadata\":");
  kjFastRender(metadata, &buf[12]);
  int dataStart = strlen(buf);

  char* body = (char*) ioVec[ioVecSize - 1].iov_base;
  snprintf(&buf[dataStart], totalLen - dataStart, ",\"body\":%s}", body);

  // Send over WebSocket
  int rc = wsSend(wsP, buf);
  KT_T(KtWsTest, "WS notification sent: sub='%s' (fd=%d, %d bytes, rc=%d)", cSubP->subId, (int) wsP->fd, (int) strlen(buf), rc);
  if (rc != 0)
  {
    KT_E("wsNotify: wsSend failed for subscription '%s'", cSubP->subId);
    notificationFailure(cSubP, "WebSocket send failed", notificationTime);
    return -1;
  }

  notificationSuccess(cSubP, notificationTime);
  return 0;
}
