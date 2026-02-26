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
#include <string.h>                                              // strlen, strdup, strcmp
#include <stdlib.h>                                              // malloc, free

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaBufferInit.h"                                 // kaBufferInit
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
#include "kjson/kjson.h"                                         // Kjson
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjChildAdd
#include "kjson/kjNavigate.h"                                    // kjNavigate
#include "kjson/kjBufferCreate.h"                                // kjBufferCreate
}

#include "cache/subCache.h"                                      // subCacheItemLookup

#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/Protocol.h"                              // Protocol, WS
#include "orionld/common/orionldState.h"                         // orionldState, orionldStateInit, orionldStateRelease
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/mongoc/mongocConnectionRelease.h"              // mongocConnectionRelease
#include "orionld/notifications/orionldAlterationsTreat.h"       // orionldAlterationsTreat
#include "orionld/serviceRoutines/orionldPostSubscriptions.h"    // orionldPostSubscriptions
#include "orionld/serviceRoutines/orionldPutEntity.h"            // orionldPutEntity
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // wsSend
#include "orionld/ws/wsMessageDispatch.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// wsErrorResponse - send an error response back over the WS connection
//
static void wsErrorResponse(WsConnection* wsP, int statusCode, const char* title, const char* detail)
{
  char buf[1024];

  snprintf(buf, sizeof(buf),
           "{\"metadata\":{\"statusCode\":%d},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/BadRequestData\","
           "\"title\":\"%s\",\"detail\":\"%s\"}}",
           statusCode, title, detail);

  wsSend(wsP, buf);
}



// -----------------------------------------------------------------------------
//
// wsRequestCleanup - lightweight cleanup for WS message handlers
//
// This replaces requestCompleted() for WS operations.  requestCompleted() is
// MHD's per-connection callback and assumes a ConnectionInfo from *con_cls,
// statistics/metrics tracking, CURL cleanup, etc. - none of which apply to WS.
//
// Calling requestCompleted() from a WS handler trashes the thread-local
// orionldState that MHD still needs for the original upgrade connection,
// causing a free()-of-invalid-pointer crash when MHD later calls
// requestCompleted() itself.
//
// What we DO need after a WS service-routine call:
//   1. Process notification alterations (if any)
//   2. Release mongoc collections and connection
//   3. Free malloc'd payload buffer (if any)
//   4. Release delayed-free buffers (orionldStateRelease)
//   5. Reset the kalloc bump allocator
//
static void wsRequestCleanup(void)
{
  // 1. Process any notification alterations
  if (orionldState.alterations != NULL)
  {
    orionldAlterationsTreat(orionldState.alterations);
    orionldState.alterations = NULL;  // Prevent MHD's requestCompleted from reprocessing freed data
  }

  // 2. Release mongoc collections and connection (same pattern as rest.cpp)
  mongoc_collection_t* contextsP      = orionldState.mongoc.contextsP;
  mongoc_collection_t* entitiesP      = orionldState.mongoc.entitiesP;
  mongoc_collection_t* subscriptionsP = orionldState.mongoc.subscriptionsP;
  mongoc_collection_t* registrationsP = orionldState.mongoc.registrationsP;

  orionldState.mongoc.contextsP      = NULL;
  orionldState.mongoc.entitiesP      = NULL;
  orionldState.mongoc.subscriptionsP = NULL;
  orionldState.mongoc.registrationsP = NULL;

  if (contextsP      != NULL)  mongoc_collection_destroy(contextsP);
  if (entitiesP      != NULL)  mongoc_collection_destroy(entitiesP);
  if (subscriptionsP != NULL)  mongoc_collection_destroy(subscriptionsP);
  if (registrationsP != NULL)  mongoc_collection_destroy(registrationsP);

  mongocConnectionRelease();

  // 3. Free malloc'd payload buffer (if any)
  if ((orionldState.in.payload != NULL) && (orionldState.in.payload != orionldState.preallocReqBuf))
  {
    free(orionldState.in.payload);
    orionldState.in.payload = NULL;
  }

  // 4. Release delayed-free buffers
  orionldStateRelease();

  // 5. Reset the kalloc bump allocator so it's clean for the next WS message
  kaBufferReset(&orionldState.kalloc, false);
}



// -----------------------------------------------------------------------------
//
// wsCreateSubscription - create a subscription via the service routine
//
// Follows the DDS pattern: orionldStateInit(NULL) + set up orionldState + call service routine.
//
// The subscription body from the WS client must include:
//   { "type": "Subscription", "entities": [...], ... }
//
// If no notification.endpoint.uri is present, a dummy "ws://localhost" is used.
// After the subscription is created, we override protocol to WS and set wsConnectionP.
//
static void wsCreateSubscription(WsConnection* wsP, KjNode* bodyP)
{
  //
  // orionldStateInit() uses the __thread orionldState - safe from the WS recv thread
  //
  orionldStateInit(NULL);

  //
  // Re-parse the body with the new orionldState.kjsonP, because the old KjNode tree
  // was parsed with a local kjsonP that will be gone.
  // We render bodyP to JSON and re-parse it.
  //
  int   bodyBufSize = kjFastRenderSize(bodyP) + 1;
  char* bodyJson    = kaAlloc(&orionldState.kalloc, bodyBufSize);

  if (bodyJson == NULL)
  {
    wsErrorResponse(wsP, 500, "Internal Error", "Out of memory");
    return;
  }
  kjFastRender(bodyP, bodyJson);

  KjNode* subP = kjParse(orionldState.kjsonP, bodyJson);
  // bodyJson NOT freed here - kjParse points into it; it lives in kalloc and is freed by wsRequestCleanup()

  if (subP == NULL)
  {
    wsErrorResponse(wsP, 400, "Parse Error", "Cannot re-parse subscription body");
    wsRequestCleanup();
    return;
  }

  //
  // Ensure a notification.endpoint.uri exists.
  // For WS subscriptions, the URI is just a placeholder - notifications go over the WS connection.
  //
  const char* navPath[] = { "notification", "endpoint", "uri", NULL };
  KjNode*     uriP      = kjNavigate(subP, navPath, NULL, NULL);

  if (uriP == NULL)
  {
    // Add notification.endpoint.uri = "ws://placeholder"
    KjNode* notifP = kjLookup(subP, "notification");

    if (notifP == NULL)
    {
      notifP = kjObject(orionldState.kjsonP, "notification");
      kjChildAdd(subP, notifP);
    }

    KjNode* endpointP = kjLookup(notifP, "endpoint");
    if (endpointP == NULL)
    {
      endpointP = kjObject(orionldState.kjsonP, "endpoint");
      kjChildAdd(notifP, endpointP);
    }

    uriP = kjString(orionldState.kjsonP, "uri", "http://ws-placeholder:0");
    kjChildAdd(endpointP, uriP);
  }

  //
  // Set up orionldState for the subscription service routine
  //
  orionldState.requestTree     = subP;
  orionldState.payloadIdNode   = kjLookup(subP, "id");
  orionldState.payloadTypeNode = kjLookup(subP, "type");

  // Decouple id and type from the request tree - pCheckSubscription expects them as separate parameters
  if (orionldState.payloadIdNode != NULL)
    kjChildRemove(subP, orionldState.payloadIdNode);
  if (orionldState.payloadTypeNode != NULL)
    kjChildRemove(subP, orionldState.payloadTypeNode);

  orionldState.tenantP         = &tenant0;
  orionldState.apiVersion      = API_VERSION_NGSILD_V1;
  orionldState.serviceP        = serviceLookupByServiceRoutine(orionldPostSubscriptions, HTTP_POST);

  //
  // If the WS connection has a tenant, use it
  //
  if (wsP->tenantName != NULL)
  {
    // TODO: look up tenant by name in tenantList
    // For now, use tenant0 (default)
  }

  KT_T(StWs, "Calling orionldPostSubscriptions from WS thread (fd=%d)", (int) wsP->fd);

  bool ok = orionldPostSubscriptions();

  KT_T(StWs, "orionldPostSubscriptions returned %s (httpStatusCode=%d, title='%s', detail='%s')",
       ok ? "true" : "false", orionldState.httpStatusCode,
       orionldState.pd.title ? orionldState.pd.title : "none",
       orionldState.pd.detail ? orionldState.pd.detail : "none");

  if (ok && orionldState.httpStatusCode == 201)
  {
    //
    // Extract the subscription ID from the Location header or from the request tree
    //
    KjNode* idP = kjLookup(subP, "id");
    if (idP == NULL)
      idP = kjLookup(subP, "_id");

    const char* subscriptionId = (idP != NULL) ? idP->value.s : "unknown";

    //
    // Set the WS connection's subscription ID
    //
    free(wsP->subscriptionId);
    wsP->subscriptionId = strdup(subscriptionId);

    //
    // Find the CachedSubscription and wire up WS
    //
    CachedSubscription* cSubP = subCacheItemLookup(orionldState.tenantP->tenant, subscriptionId);
    if (cSubP != NULL)
    {
      cSubP->protocol       = WS;
      cSubP->wsConnectionP  = wsP;
      cSubP->wsFd           = (int) wsP->fd;

      KT_T(StWs, "Subscription '%s' wired to WS connection (fd=%d)", subscriptionId, (int) wsP->fd);
    }
    else
      KT_W("Subscription '%s' not found in cache after creation", subscriptionId);

    // Send success response with the subscription ID
    char response[512];
    snprintf(response, sizeof(response),
             "{\"metadata\":{\"statusCode\":201,\"subscriptionId\":\"%s\"},\"body\":{\"id\":\"%s\",\"status\":\"created\"}}",
             subscriptionId, subscriptionId);
    wsSend(wsP, response);
  }
  else
  {
    // Send error response
    char response[512];
    snprintf(response, sizeof(response),
             "{\"metadata\":{\"statusCode\":%d},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/BadRequestData\","
             "\"title\":\"Subscription creation failed\",\"detail\":\"httpStatusCode=%d\"}}",
             orionldState.httpStatusCode, orionldState.httpStatusCode);
    wsSend(wsP, response);
  }

  //
  // Cleanup
  //
  wsRequestCleanup();
}



// -----------------------------------------------------------------------------
//
// wsPutEntity - replace an entity via the service routine over WebSocket
//
// Message format:
//   {
//     "metadata": { "operation": "putEntity" },
//     "body": { "id": "urn:ngsi-ld:...", "type": "CircleActivity", "attr1": {...}, ... }
//   }
//
// The entity id MUST be in the body (same as NGSI-LD API allows in request body).
//
static void wsPutEntity(WsConnection* wsP, KjNode* metadataP, KjNode* bodyP)
{
  KjNode* entityIdP = kjLookup(bodyP, "id");

  if (entityIdP == NULL || entityIdP->type != KjString)
  {
    wsErrorResponse(wsP, 400, "Bad Request", "putEntity requires 'id' in the body");
    return;
  }

  KjNode* actionP = kjLookup(bodyP, "action");
  const char* action = (actionP != NULL && actionP->type == KjString) ? actionP->value.s : "n/a";

  orionldStateInit(NULL);

  //
  // Re-parse the body with the new orionldState.kjsonP
  //
  int   bodyBufSize = kjFastRenderSize(bodyP) + 1;
  char* bodyJson    = kaAlloc(&orionldState.kalloc, bodyBufSize);

  if (bodyJson == NULL)
  {
    wsErrorResponse(wsP, 500, "Internal Error", "Out of memory");
    return;
  }
  kjFastRender(bodyP, bodyJson);

  KjNode* entityP = kjParse(orionldState.kjsonP, bodyJson);

  if (entityP == NULL)
  {
    wsErrorResponse(wsP, 400, "Parse Error", "Cannot re-parse entity body");
    wsRequestCleanup();
    return;
  }

  //
  // Set up orionldState for orionldPutEntity
  //
  orionldState.requestTree     = entityP;
  orionldState.payloadIdNode   = kjLookup(entityP, "id");
  orionldState.payloadTypeNode = kjLookup(entityP, "type");
  orionldState.tenantP         = &tenant0;  // TODO: look up tenant by wsP->tenantName
  orionldState.apiVersion      = API_VERSION_NGSILD_V1;
  orionldState.serviceP        = serviceLookupByServiceRoutine(orionldPutEntity, HTTP_PUT);

  //
  // orionldPutEntity uses wildcard[0] as the entity ID (from URL in HTTP, from metadata in WS)
  //
  orionldState.wildcard[0] = entityIdP->value.s;

  bool ok = orionldPutEntity();

  KT_T(KtWsTest, "WS putEntity: %s (action=%s, rc=%d)", entityIdP->value.s, action, orionldState.httpStatusCode);

  if (ok && orionldState.httpStatusCode == 204)
  {
    char response[512];
    snprintf(response, sizeof(response),
             "{\"metadata\":{\"statusCode\":204,\"entityId\":\"%s\"},\"body\":{\"status\":\"replaced\"}}",
             entityIdP->value.s);
    wsSend(wsP, response);
  }
  else
  {
    char response[512];
    snprintf(response, sizeof(response),
             "{\"metadata\":{\"statusCode\":%d},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/BadRequestData\","
             "\"title\":\"%s\",\"detail\":\"%s\"}}",
             orionldState.httpStatusCode,
             orionldState.pd.title ? orionldState.pd.title : "Entity operation failed",
             orionldState.pd.detail ? orionldState.pd.detail : "unknown error");
    wsSend(wsP, response);
  }

  //
  // Cleanup
  //
  wsRequestCleanup();
}



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
void wsMessageDispatch(WsConnection* wsP, const char* message, size_t messageLen)
{
  //
  // Parse the incoming JSON envelope using a temporary Kjson instance
  // (we're in the WS recv thread, not an MHD thread)
  //
  // NOTE: We parse once here to extract metadata/body structure,
  //       then wsCreateSubscription re-parses with orionldState.kjsonP
  //
  char    parseBuffer[8192];
  Kjson   kjson;
  KAlloc  kalloc;

  memset(&kjson, 0, sizeof(kjson));
  kaBufferInit(&kalloc, parseBuffer, sizeof(parseBuffer), 8 * 1024, NULL, "WS parse buffer");
  Kjson*  kjsonP = kjBufferCreate(&kjson, &kalloc);

  // Make a mutable copy of the message for parsing
  char* messageCopy = (char*) malloc(messageLen + 1);
  if (messageCopy == NULL)
  {
    KT_E("Out of memory in wsMessageDispatch");
    return;
  }
  memcpy(messageCopy, message, messageLen);
  messageCopy[messageLen] = 0;

  KjNode* tree = kjParse(kjsonP, messageCopy);
  if (tree == NULL)
  {
    KT_W("WS message parse failed");
    wsErrorResponse(wsP, 400, "JSON Parse Error", "Cannot parse incoming WebSocket message");
    free(messageCopy);
    return;
  }

  // Extract metadata and body
  KjNode* metadataP = kjLookup(tree, "metadata");
  KjNode* bodyP     = kjLookup(tree, "body");

  if (metadataP == NULL || bodyP == NULL)
  {
    KT_W("WS message missing 'metadata' or 'body'");
    wsErrorResponse(wsP, 400, "Bad Request", "WebSocket message must contain 'metadata' and 'body'");
    free(messageCopy);
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

  KT_T(KtWsTest, "WS request: %s (fd=%d, tenant: %s)",
       operation, (int) wsP->fd, wsP->tenantName ? wsP->tenantName : "default");
  KT_T(StWs, "WS message received: operation='%s' (fd=%d)", operation, (int) wsP->fd);

  //
  // Dispatch based on operation
  //
  if (strcmp(operation, "createSubscription") == 0)
  {
    wsCreateSubscription(wsP, bodyP);
  }
  else if (strcmp(operation, "putEntity") == 0)
  {
    wsPutEntity(wsP, metadataP, bodyP);
  }
  else if (strcmp(operation, "patchSubscription") == 0)
  {
    KjNode* subIdP = kjLookup(metadataP, "subscriptionId");
    if (subIdP == NULL || subIdP->type != KjString)
    {
      wsErrorResponse(wsP, 400, "Bad Request", "patchSubscription requires 'subscriptionId' in metadata");
      free(messageCopy);
      return;
    }

    KT_T(StWs, "PatchSubscription over WS - subId: %s (not yet implemented)", subIdP->value.s);

    // TODO: Wire up orionldPatchSubscription in the same way as createSubscription
    char response[512];
    snprintf(response, sizeof(response),
             "{\"metadata\":{\"statusCode\":501},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/OperationNotSupported\","
             "\"title\":\"Not Yet Implemented\",\"detail\":\"patchSubscription over WS - coming soon\"}}");
    wsSend(wsP, response);
  }
  else
  {
    KT_W("WS unsupported operation: '%s'", operation);
    wsErrorResponse(wsP, 400, "Unsupported Operation",
                    "Supported operations: 'createSubscription', 'putEntity', 'patchSubscription'");
  }

  free(messageCopy);
}
