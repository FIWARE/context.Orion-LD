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
extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
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
#include "orionld/serviceRoutines/orionldPostEntities.h"         // orionldPostEntities
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // wsSend
#include "orionld/ws/wsErrorResponse.h"                          // wsErrorResponse
#include "orionld/ws/wsRequestCleanup.h"                         // wsRequestCleanup
#include "orionld/ws/wsCreateEntity.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// wsCreateEntity - create an entity via the service routine over WebSocket
//
// Message format:
//   {
//     "metadata": { "operation": "createEntity" },
//     "body": { "id": "urn:ngsi-ld:...", "type": "..." }
//   }
//
// Only id and type are required - no attributes needed.
// 201 on success, 409 if entity already exists.
//
void wsCreateEntity(WsConnection* wsP, KjNode* metadataP, KjNode* bodyP)
{
  KjNode* entityIdP = kjLookup(bodyP, "id");

  if (entityIdP == NULL || entityIdP->type != KjString)
  {
    wsErrorResponse(wsP, 400, "Bad Request", "createEntity requires 'id' in the body");
    return;
  }

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
  // Set up orionldState for orionldPostEntities
  //
  orionldState.requestTree     = entityP;
  orionldState.payloadIdNode   = kjLookup(entityP, "id");
  orionldState.payloadTypeNode = kjLookup(entityP, "type");
  orionldState.tenantP         = &tenant0;
  orionldState.apiVersion      = API_VERSION_NGSILD_V1;
  orionldState.serviceP        = serviceLookupByServiceRoutine(orionldPostEntities, HTTP_POST);

  // Expand the entity type using the context
  if (orionldState.payloadTypeNode != NULL)
    orionldState.payloadTypeNode->value.s = orionldContextItemExpand(orionldState.contextP, orionldState.payloadTypeNode->value.s, true, NULL);

  // Decouple id and type from the request tree - orionldPostEntities expects them as separate parameters
  if (orionldState.payloadIdNode != NULL)
    kjChildRemove(entityP, orionldState.payloadIdNode);
  if (orionldState.payloadTypeNode != NULL)
    kjChildRemove(entityP, orionldState.payloadTypeNode);

  bool ok = orionldPostEntities();

  KT_T(StWs, "WS createEntity: %s (rc=%d)", entityIdP->value.s, orionldState.httpStatusCode);

  if (ok && orionldState.httpStatusCode == 201)
  {
    char response[512];
    snprintf(response, sizeof(response),
             "{\"metadata\":{\"statusCode\":201,\"entityId\":\"%s\"},\"body\":{\"status\":\"created\"}}",
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
             orionldState.pd.title ? orionldState.pd.title : "Entity creation failed",
             orionldState.pd.detail ? orionldState.pd.detail : "unknown error");
    wsSend(wsP, response);
  }

  wsRequestCleanup();
}
