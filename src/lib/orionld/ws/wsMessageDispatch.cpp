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
#include <stdlib.h>                                              // malloc, free

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaBufferInit.h"                                 // kaBufferInit
#include "kjson/kjson.h"                                         // Kjson
#include "kjson/kjBufferCreate.h"                                // kjBufferCreate
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjBuilder.h"                                     // kjChildRemove
}

#include "orionld/common/orionldState.h"                         // orionldState, orionldStateInit
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // wsSend
#include "orionld/ws/wsErrorResponse.h"                          // wsErrorResponse
#include "orionld/ws/wsRequestCleanup.h"                         // wsRequestCleanup
#include "orionld/ws/wsServiceLookup.h"                          // wsServiceLookup, WsService
#include "orionld/ws/wsMessageDispatch.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// wsMessageDispatch - dispatch an incoming WS message
//
// This is the WS equivalent of mhdConnectionInit + mhdConnectionTreat combined.
// All common setup happens here — the WS service routines are just function pointers
// in the dispatch table (like troeRoutine is for TRoE).
//
// Message format (same as MQTT):
//   {"metadata": {...}, "body": {...}}
//
// The metadata object contains headers-like info:
//   - "operation":      "createSubscription" | "createEntity" | "putEntity" | "patchSubscription"
//   - "Tenant":         optional tenant name
//   - "Content-Type":   optional (default: application/json)
//   - "subscriptionId": required for PATCH (in metadata)
//   - "entityId":       required for putEntity (entity ID for wildcard[0])
//
void wsMessageDispatch(WsConnection* wsP, char* message, size_t messageLen)
{
  //
  // 1. Initialize orionldState for this WS message
  //    Same thread as wsUpgrade/wsReceiveLoop, so __thread variables are safe
  //
  orionldStateInit(NULL);

  //
  // 2. Parse the incoming JSON envelope using a malloc-backed Kjson.
  //    NOT using kalloc — the tree must survive through the service routine call.
  //    kalloc is reserved for the service routine's temporary allocations.
  //
  //    The 'message' buffer is malloc'd by MHD_websocket_decode and stays alive
  //    until the caller (wsReceiveLoop) frees it.
  //    kjParse works in-place on 'message', so KjNode string values point into it.
  //
  char*   parseBuf = (char*) malloc(8192);
  if (parseBuf == NULL)
  {
    KT_E("Out of memory allocating WS parse buffer");
    wsErrorResponse(wsP, 500, "Internal Error", "Out of memory");
    return;
  }

  Kjson   kjson;
  KAlloc  kalloc;
  memset(&kjson, 0, sizeof(kjson));
  kaBufferInit(&kalloc, parseBuf, 8192, 8 * 1024, NULL, "WS parse buffer");
  Kjson*  kjsonP = kjBufferCreate(&kjson, &kalloc);

  KjNode* tree = kjParse(kjsonP, message);
  if (tree == NULL)
  {
    KT_W("WS message parse failed");
    wsErrorResponse(wsP, 400, "JSON Parse Error", "Cannot parse incoming WebSocket message");
    free(parseBuf);
    return;
  }

  //
  // 3. Extract metadata and body from the envelope
  //
  KjNode* metadataP = kjLookup(tree, "metadata");
  KjNode* bodyP     = kjLookup(tree, "body");

  if (metadataP == NULL || bodyP == NULL)
  {
    KT_W("WS message missing 'metadata' or 'body'");
    wsErrorResponse(wsP, 400, "Bad Request", "WebSocket message must contain 'metadata' and 'body'");
    free(parseBuf);
    return;
  }

  //
  // 4. Extract operation from metadata — default to "createSubscription"
  //
  KjNode*     operationP = kjLookup(metadataP, "operation");
  const char* operation  = "createSubscription";

  if (operationP != NULL && operationP->type == KjString)
    operation = operationP->value.s;

  KT_T(KtWsTest, "WS request: %s (fd=%d, tenant: %s)", operation, (int) wsP->fd, wsP->tenantName ? wsP->tenantName : "default");
  KT_T(StWs, "WS message received: operation='%s' (fd=%d)", operation, (int) wsP->fd);

  //
  // 5. Look up the WS service — includes the HTTP service routine and verb
  //
  WsService* wsServiceP = wsServiceLookup(operation);
  if (wsServiceP == NULL)
  {
    KT_W("WS unsupported operation: '%s'", operation);
    wsErrorResponse(wsP, 400, "Unsupported Operation", "Supported operations: 'createEntity', 'createSubscription', 'putEntity', 'patchSubscription'");
    free(parseBuf);
    return;
  }

  //
  // 6. Common setup — what mhdConnectionInit/Treat does for HTTP
  //
  orionldState.requestTree     = bodyP;
  orionldState.payloadIdNode   = kjLookup(bodyP, "id");
  orionldState.payloadTypeNode = kjLookup(bodyP, "type");
  orionldState.tenantP         = &tenant0;
  orionldState.apiVersion      = API_VERSION_NGSILD_V1;
  orionldState.serviceP        = serviceLookupByServiceRoutine(wsServiceP->serviceRoutine, wsServiceP->verb);

  //
  // Decouple id and type only if the service expects it (same as mhdConnectionTreat)
  //
  if ((orionldState.serviceP != NULL) && (orionldState.serviceP->options & ORIONLD_SERVICE_OPTION_PREFETCH_ID_AND_TYPE))
  {
    if (orionldState.payloadIdNode != NULL)
      kjChildRemove(bodyP, orionldState.payloadIdNode);
    if (orionldState.payloadTypeNode != NULL)
      kjChildRemove(bodyP, orionldState.payloadTypeNode);
  }

  // Expand the entity type (same as mhdConnectionTreat)
  if ((orionldState.serviceP != NULL) &&
      (orionldState.serviceP->options & ORIONLD_SERVICE_OPTION_EXPAND_TYPE) &&
      (orionldState.payloadTypeNode != NULL))
  {
    orionldState.payloadTypeNode->value.s = orionldContextItemExpand(orionldState.contextP, orionldState.payloadTypeNode->value.s, true, NULL);
  }

  // Entity ID from metadata for operations that need wildcard[0] (putEntity, patchSubscription, etc.)
  KjNode* entityIdP = kjLookup(metadataP, "entityId");
  if (entityIdP != NULL && entityIdP->type == KjString)
    orionldState.wildcard[0] = entityIdP->value.s;
  else if (orionldState.payloadIdNode != NULL)
    orionldState.wildcard[0] = orionldState.payloadIdNode->value.s;

  // Subscription ID from metadata (for patchSubscription)
  KjNode* subIdP = kjLookup(metadataP, "subscriptionId");
  if (subIdP != NULL && subIdP->type == KjString)
    orionldState.wildcard[0] = subIdP->value.s;

  // TODO: look up tenant by wsP->tenantName in tenantList

  //
  // 7. Pre-processing (e.g. subscription: ensure notification.endpoint.uri placeholder)
  //
  if (wsServiceP->preRoutine != NULL)
    wsServiceP->preRoutine(wsP);

  //
  // 8. Call the HTTP service routine
  //
  KT_T(StWs, "Calling service routine for '%s' (fd=%d)", operation, (int) wsP->fd);
  bool ok = wsServiceP->serviceRoutine();

  KT_T(StWs, "Service routine returned %s (httpStatusCode=%d, title='%s', detail='%s')",
       ok ? "true" : "false", orionldState.httpStatusCode,
       orionldState.pd.title ? orionldState.pd.title : "none",
       orionldState.pd.detail ? orionldState.pd.detail : "none");

  //
  // 9. Post-processing (e.g. subscription: wire WS connection to cached subscription)
  //
  if (ok && orionldState.httpStatusCode < 300 && wsServiceP->postRoutine != NULL)
    wsServiceP->postRoutine(wsP);

  //
  // 10. Render response and save IDs while kalloc is still alive
  //     The responseTree and auto-generated IDs are in kalloc — must capture before wsRequestCleanup resets it.
  //
  char* responseJson = NULL;

  if (orionldState.responseTree != NULL)
  {
    int   responseSize = kjFastRenderSize(orionldState.responseTree) + 1;
    responseJson = (char*) malloc(responseSize);
    if (responseJson != NULL)
      kjFastRender(orionldState.responseTree, responseJson);
  }

  //
  // Save the entity/subscription ID for the response metadata.
  // It may come from payloadIdNode (client-provided) or from the request tree (auto-generated by the service routine).
  //
  char* savedId = NULL;
  if (orionldState.payloadIdNode != NULL)
    savedId = strdup(orionldState.payloadIdNode->value.s);
  else if (orionldState.requestTree != NULL)
  {
    KjNode* idP = kjLookup(orionldState.requestTree, "id");
    if (idP == NULL)
      idP = kjLookup(orionldState.requestTree, "_id");
    if (idP != NULL && idP->type == KjString)
      savedId = strdup(idP->value.s);
  }

  int savedStatusCode = orionldState.httpStatusCode;
  char* savedTitle    = orionldState.pd.title  ? strdup(orionldState.pd.title)  : NULL;
  char* savedDetail   = orionldState.pd.detail ? strdup(orionldState.pd.detail) : NULL;

  //
  // 11. Cleanup — resets kalloc, releases mongoc, processes alterations
  //
  wsRequestCleanup();

  //
  // 12. Send response over WebSocket
  //
  char  responseBuf[1024];

  if (ok && savedStatusCode < 300)
  {
    //
    // Build metadata with statusCode and optional IDs
    //
    char metadataBuf[512];
    int  mLen = snprintf(metadataBuf, sizeof(metadataBuf), "{\"statusCode\":%d", savedStatusCode);

    if (savedId != NULL)
      mLen += snprintf(&metadataBuf[mLen], sizeof(metadataBuf) - mLen, ",\"subscriptionId\":\"%s\"", savedId);

    snprintf(&metadataBuf[mLen], sizeof(metadataBuf) - mLen, "}");

    if (responseJson != NULL)
      snprintf(responseBuf, sizeof(responseBuf), "{\"metadata\":%s,\"body\":%s}", metadataBuf, responseJson);
    else
      snprintf(responseBuf, sizeof(responseBuf), "{\"metadata\":%s,\"body\":{\"status\":\"ok\"}}", metadataBuf);
  }
  else
  {
    snprintf(responseBuf, sizeof(responseBuf),
             "{\"metadata\":{\"statusCode\":%d},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/BadRequestData\","
             "\"title\":\"%s\",\"detail\":\"%s\"}}",
             savedStatusCode,
             savedTitle  ? savedTitle  : "Operation failed",
             savedDetail ? savedDetail : "unknown error");
  }

  wsSend(wsP, responseBuf);

  //
  // 13. Free malloc'd buffers
  //
  free(responseJson);
  free(parseBuf);
  free(savedId);
  free(savedTitle);
  free(savedDetail);
}
