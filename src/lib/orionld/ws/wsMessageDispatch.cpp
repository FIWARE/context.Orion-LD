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
#include <string.h>                                              // strlen, strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaBufferInit.h"                                 // kaBufferInit
#include "kjson/kjson.h"                                         // Kjson
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBufferCreate.h"                                // kjBufferCreate
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsErrorResponse.h"                          // wsErrorResponse
#include "orionld/ws/wsServiceLookup.h"                          // wsServiceLookup, WsServiceRoutine
#include "orionld/ws/wsMessageDispatch.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// wsMessageDispatch - dispatch an incoming WS message
//
// Message format (same as MQTT):
//   {"metadata": {...}, "body": {...}}
//
// The metadata object contains headers-like info:
//   - "operation":    "createSubscription" | "putEntity" | "patchSubscription"
//   - "Tenant":       optional tenant name
//   - "Content-Type": optional (default: application/json)
//   - "subscriptionId": required for PATCH
//
// The body contains the NGSI-LD payload (subscription or patch).
//
void wsMessageDispatch(WsConnection* wsP, char* message, size_t messageLen)
{
  //
  // Parse the incoming JSON envelope using a temporary Kjson instance
  // (we're in the WS recv thread, not an MHD thread)
  //
  // NOTE: We parse once here to extract metadata/body structure,
  //       then the handler re-parses with orionldState.kjsonP
  //
  // The 'message' buffer is already mutable (malloc'd by MHD_websocket_decode)
  // and stays alive until the caller frees it, so kjParse can work in-place.
  //
  char    parseBuffer[8192];
  Kjson   kjson;
  KAlloc  kalloc;

  memset(&kjson, 0, sizeof(kjson));
  kaBufferInit(&kalloc, parseBuffer, sizeof(parseBuffer), 8 * 1024, NULL, "WS parse buffer");
  Kjson*  kjsonP = kjBufferCreate(&kjson, &kalloc);

  KjNode* tree = kjParse(kjsonP, message);
  if (tree == NULL)
  {
    KT_W("WS message parse failed");
    wsErrorResponse(wsP, 400, "JSON Parse Error", "Cannot parse incoming WebSocket message");
    return;
  }

  // Extract metadata and body
  KjNode* metadataP = kjLookup(tree, "metadata");
  KjNode* bodyP     = kjLookup(tree, "body");

  if (metadataP == NULL || bodyP == NULL)
  {
    KT_W("WS message missing 'metadata' or 'body'");
    wsErrorResponse(wsP, 400, "Bad Request", "WebSocket message must contain 'metadata' and 'body'");
    return;
  }

  // Extract operation from metadata
  KjNode* operationP = kjLookup(metadataP, "operation");

  //
  // If no 'operation' in metadata, default to "createSubscription"
  // (the first message on a WS connection is typically a subscription creation)
  //
  const char* operation = "createSubscription";

  if (operationP != NULL && operationP->type == KjString)
    operation = operationP->value.s;

  KT_T(KtWsTest, "WS request: %s (fd=%d, tenant: %s)", operation, (int) wsP->fd, wsP->tenantName ? wsP->tenantName : "default");
  KT_T(StWs, "WS message received: operation='%s' (fd=%d)", operation, (int) wsP->fd);

  //
  // Dispatch based on operation
  //
  WsServiceRoutine routine = wsServiceLookup(operation);
  if (routine == NULL)
  {
    KT_W("WS unsupported operation: '%s'", operation);
    wsErrorResponse(wsP, 400, "Unsupported Operation", "Supported operations: 'createEntity', 'createSubscription', 'putEntity', 'patchSubscription'");
    return;
  }

  routine(wsP, metadataP, bodyP);
}
