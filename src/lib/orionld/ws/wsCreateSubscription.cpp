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
#include <string.h>                                              // strdup
#include <stdlib.h>                                              // free

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjChildAdd
#include "kjson/kjNavigate.h"                                    // kjNavigate
}

#include "cache/subCache.h"                                      // subCacheItemLookup

#include "orionld/types/Protocol.h"                              // Protocol, WS
#include "orionld/common/orionldState.h"                         // orionldState, orionldStateInit
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/serviceRoutines/orionldPostSubscriptions.h"    // orionldPostSubscriptions
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // wsSend
#include "orionld/ws/wsErrorResponse.h"                          // wsErrorResponse
#include "orionld/ws/wsRequestCleanup.h"                         // wsRequestCleanup
#include "orionld/ws/wsCreateSubscription.h"                     // Own interface



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
void wsCreateSubscription(WsConnection* wsP, KjNode* metadataP, KjNode* bodyP)
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

    uriP = kjString(orionldState.kjsonP, "uri", "ws://ws-placeholder:0");
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
