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
#include <stdio.h>                                               // snprintf

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsSend.h"                                   // wsSend
#include "orionld/ws/wsErrorResponse.h"                          // wsErrorResponse
#include "orionld/ws/wsPatchSubscription.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// wsPatchSubscription - patch a subscription via the service routine over WebSocket
//
// TODO: Wire up orionldPatchSubscription in the same way as createSubscription
//
void wsPatchSubscription(WsConnection* wsP, KjNode* metadataP, KjNode* bodyP)
{
  KjNode* subIdP = kjLookup(metadataP, "subscriptionId");
  if (subIdP == NULL || subIdP->type != KjString)
  {
    wsErrorResponse(wsP, 400, "Bad Request", "patchSubscription requires 'subscriptionId' in metadata");
    return;
  }

  KT_T(StWs, "PatchSubscription over WS - subId: %s (not yet implemented)", subIdP->value.s);

  char response[512];
  snprintf(response, sizeof(response),
           "{\"metadata\":{\"statusCode\":501},\"body\":{\"type\":\"https://uri.etsi.org/ngsi-ld/errors/OperationNotSupported\","
           "\"title\":\"Not Yet Implemented\",\"detail\":\"patchSubscription over WS - coming soon\"}}");
  wsSend(wsP, response);
}
