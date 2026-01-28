/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include <memory>                                           // std::shared_ptr

#include "ddsenabler/DDSEnabler.hpp"                        // DDSEnabler

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjRender.h"                                 // kjRender
#include "kjson/kjRenderSize.h"                             // kjRenderSize
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // KT_T trace levels
#include "ftClient/ftErrorResponse.h"                       // ftErrorResponse



extern bool                                              ddsSupport;
extern char*                                             ddsServiceName;
extern std::shared_ptr<eprosima::ddsenabler::DDSEnabler> ddsEnabler;
// -----------------------------------------------------------------------------
//
// postDdsServiceReply -
//
// Payload body:
// {
//   "service": "service-name",      // Optional if --ddsService was used
//   "requestId": 12345,             // Required - must match a received request
//   "body": { ... }                 // The reply payload
// }
//
KjNode* postDdsServiceReply(int* statusCodeP)
{
  if (ddsSupport == false)
  {
    *statusCodeP = 501;
    return ftErrorResponse(501, "DDS Support not enabled", "restart with --dds");
  }

  if (orionldState.requestTree == NULL)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Missing payload body", "POST /dds/service/reply requires a JSON payload");
  }

  // Get service name - from payload or from CLI
  KjNode*     serviceNodeP = kjLookup(orionldState.requestTree, "service");
  const char* serviceName  = (serviceNodeP != NULL) ? serviceNodeP->value.s : ddsServiceName;

  if (serviceName == NULL || serviceName[0] == 0)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Missing service name", "either use --ddsService CLI option or include 'service' in payload");
  }

  // Get request ID
  KjNode* reqIdNodeP = kjLookup(orionldState.requestTree, "requestId");
  if (reqIdNodeP == NULL)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Missing requestId", "requestId is required in payload");
  }
  if (reqIdNodeP->type != KjInt)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Invalid requestId", "requestId must be an integer");
  }
  uint64_t requestId = reqIdNodeP->value.i;

  // Get reply body
  KjNode* bodyNodeP = kjLookup(orionldState.requestTree, "body");
  if (bodyNodeP == NULL)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Missing body", "body is required in payload");
  }

  // Render the body to JSON string
  int   bufSize = kjRenderSize(orionldState.kjsonP, bodyNodeP) + 1;
  char* json    = kaAlloc(&orionldState.kalloc, bufSize);
  kjRender(orionldState.kjsonP, bodyNodeP, json);

  KT_T(StDds, "Sending DDS service reply (service: '%s', reqId: %llu): %s", serviceName, requestId, json);

  // Send the reply via DDS
  bool result = ddsEnabler->send_service_reply(serviceName, json, requestId);

  if (result == false)
  {
    KT_E("Failed to send DDS service reply");
    *statusCodeP = 500;
    return ftErrorResponse(500, "Failed to send DDS service reply", "check DDS logs for details");
  }

  KT_T(StDds, "DDS service reply sent successfully");
  *statusCodeP = 204;
  return NULL;
}
