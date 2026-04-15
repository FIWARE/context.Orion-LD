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
#include <stdlib.h>                                              // free
#include <string.h>                                              // strncmp, strstr

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjInteger, kjChildAdd
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjClone.h"                                       // kjClone
}

#include "orionld/types/DdsService.h"                            // DdsService, DdsServiceInstance
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/serviceRoutines/orionldPatchEntity2.h"         // orionldPatchEntity2
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/dds/ddsServiceLookup.h"                        // ddsServiceLookup
#include "orionld/dds/ddsServiceReplyNotification.h"             // Own interface



// -----------------------------------------------------------------------------
//
// ddsInstancePop - find the in-flight request, unlink it from the service's
// instances list, and return it (caller is responsible for freeing).
//
static DdsServiceInstance* ddsInstancePop(DdsService* serviceP, uint64_t requestId)
{
  DdsServiceInstance* prev = NULL;

  for (DdsServiceInstance* dsiP = serviceP->instances; dsiP != NULL; dsiP = dsiP->next)
  {
    if (dsiP->requestId == requestId)
    {
      if (prev != NULL)
        prev->next = dsiP->next;
      else
        serviceP->instances = dsiP->next;

      return dsiP;
    }
    prev = dsiP;
  }
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsInstanceFree - tear down a popped DdsServiceInstance and release its memory.
//
static void ddsInstanceFree(DdsServiceInstance* dsiP)
{
  if (dsiP == NULL)
    return;
  kaBufferReset(&dsiP->kalloc, KFALSE);
  free(dsiP);
}



// -----------------------------------------------------------------------------
//
// stringPropertyNode - convenience: build a Property whose value is a string.
//
static KjNode* stringPropertyNode(const char* name, const char* value)
{
  KjNode* prop  = kjObject(orionldState.kjsonP, name);
  KjNode* tNode = kjString(orionldState.kjsonP, "type",  "Property");
  KjNode* vNode = kjString(orionldState.kjsonP, "value", (value != NULL)? value : "");

  kjChildAdd(prop, tNode);
  kjChildAdd(prop, vNode);
  return prop;
}



// -----------------------------------------------------------------------------
//
// integerPropertyNode - convenience: build a Property whose value is an integer.
//
static KjNode* integerPropertyNode(const char* name, long long value)
{
  KjNode* prop  = kjObject(orionldState.kjsonP, name);
  KjNode* tNode = kjString(orionldState.kjsonP, "type",  "Property");
  KjNode* vNode = kjInteger(orionldState.kjsonP, "value", value);

  kjChildAdd(prop, tNode);
  kjChildAdd(prop, vNode);
  return prop;
}



// -----------------------------------------------------------------------------
//
// extractReplyMetadata -
//
// The DDS Enabler delivers a reply as a JSON object structured like:
//
//   {
//     "id":   "<participantId>",     // top-level
//     "type": "fastdds",             // protocol marker
//     "rr/<service>Reply": {
//       "type": "<ddsDataType>",     // e.g. example_interfaces::srv::dds_::AddTwoInts_Response_
//       "data": {
//         "<xId>": <actual reply payload>
//       }
//     }
//   }
//
// This function teases out the four fields we want to surface as Properties on
// the reply sub-attribute and returns the actual reply payload (a KjNode that
// is still parented to 'replyTree' and must be detached/cloned by the caller).
//
// All output pointers are filled with NULL when the corresponding field can't
// be found - the caller decides how to react.
//
static KjNode* extractReplyMetadata
(
  KjNode*       replyTree,
  const char**  participantIdP,
  const char**  ddsDataTypeP,
  const char**  xIdP
)
{
  *participantIdP = NULL;
  *ddsDataTypeP   = NULL;
  *xIdP           = NULL;

  if ((replyTree == NULL) || (replyTree->type != KjObject))
    return NULL;

  // Top-level "id" -> participantId
  KjNode* idNode = kjLookup(replyTree, "id");
  if ((idNode != NULL) && (idNode->type == KjString))
    *participantIdP = idNode->value.s;

  // Find the "rr/<something>Reply" child
  KjNode* rrNode = NULL;
  for (KjNode* child = replyTree->value.firstChildP; child != NULL; child = child->next)
  {
    if ((child->name != NULL) && (strncmp(child->name, "rr/", 3) == 0))
    {
      rrNode = child;
      break;
    }
  }

  if ((rrNode == NULL) || (rrNode->type != KjObject))
    return NULL;

  KjNode* typeNode = kjLookup(rrNode, "type");
  if ((typeNode != NULL) && (typeNode->type == KjString))
    *ddsDataTypeP = typeNode->value.s;

  KjNode* dataNode = kjLookup(rrNode, "data");
  if ((dataNode == NULL) || (dataNode->type != KjObject))
    return NULL;

  // The single child of "data" is keyed by xId; its value is the actual reply payload.
  KjNode* xIdValueNode = dataNode->value.firstChildP;
  if (xIdValueNode == NULL)
    return NULL;

  *xIdP = xIdValueNode->name;
  return xIdValueNode;
}



// -----------------------------------------------------------------------------
//
// buildSubAttribute - build a "request" or "reply" sub-attribute Property
// with all its NGSI-LD sub-sub-attribute Properties (requestId, xId,
// participantId, ddsDataType, publishedAt) and the cloned payload as its value.
//
// 'subName'        : "request" or "reply"
// 'payloadValue'   : KjNode that becomes the Property's value (will be reused -
//                     caller must pass a node it doesn't need elsewhere)
// 'requestId'      : the DDS request id assigned by the enabler
// 'xId'            : may be NULL (request side typically has none)
// 'participantId'  : may be NULL (request side typically has none)
// 'ddsDataType'    : may be NULL on the request side
// 'publishedAt'    : seconds since epoch
//
static KjNode* buildSubAttribute
(
  const char*  subName,
  KjNode*      payloadValue,
  uint64_t     requestId,
  const char*  xId,
  const char*  participantId,
  const char*  ddsDataType,
  int64_t      publishedAt
)
{
  KjNode* sub      = kjObject(orionldState.kjsonP, subName);
  KjNode* typeNode = kjString(orionldState.kjsonP, "type", "Property");

  payloadValue->name = (char*) "value";

  kjChildAdd(sub, typeNode);
  kjChildAdd(sub, payloadValue);

  kjChildAdd(sub, integerPropertyNode("requestId",   (long long) requestId));
  if (xId           != NULL) kjChildAdd(sub, stringPropertyNode("xId",            xId));
  if (participantId != NULL) kjChildAdd(sub, stringPropertyNode("participantId",  participantId));
  if (ddsDataType   != NULL) kjChildAdd(sub, stringPropertyNode("ddsDataType",    ddsDataType));
  kjChildAdd(sub, integerPropertyNode("publishedAt", (long long) publishedAt));

  return sub;
}



// -----------------------------------------------------------------------------
//
// ddsServiceReplyNotification -
//
// Called by the DDS Enabler when a reply arrives for a service request that was
// previously sent by the broker (acting as client).
//
// On reply we patch the service's mapped NGSI-LD attribute to add two
// sub-attribute Properties:
//
//   "<attributeName>": {
//     "type": "Property",
//     "request": {
//       "type": "Property",
//       "value": <original request payload>,
//       "requestId":   { "type": "Property", "value": <id> },
//       "publishedAt": { "type": "Property", "value": <seconds since epoch> }
//     },
//     "reply": {
//       "type": "Property",
//       "value": <actual reply payload (xId-keyed value, unwrapped)>,
//       "requestId":     { "type": "Property", "value": <id> },
//       "xId":           { "type": "Property", "value": <xId> },
//       "participantId": { "type": "Property", "value": <participantId> },
//       "ddsDataType":   { "type": "Property", "value": <ddsDataType> },
//       "publishedAt":   { "type": "Property", "value": <seconds since epoch> }
//     }
//   }
//
void ddsServiceReplyNotification
(
  const char* serviceName,
  const char* json,
  uint64_t    requestId,
  int64_t     publishTime
)
{
  KT_T(StDdsService, "Got a Service Reply Notification (service: '%s', req: %llu): '%s'", serviceName, requestId, json);

  // The enabler passes publishTime as nanoseconds since epoch - convert to
  // seconds so reply.publishedAt matches request.publishedAt (which the
  // broker captures with time(NULL)).
  publishTime /= 1000000000LL;

  DdsService* serviceP = ddsServiceLookup(serviceName);
  if (serviceP == NULL)
  {
    KT_W("Service '%s' not found", serviceName);
    return;
  }

  //
  // Find and unlink the in-flight instance for this request id.
  //
  DdsServiceInstance* dsiP = ddsInstancePop(serviceP, requestId);
  if (dsiP == NULL)
    KT_W("Instance '%llu' of service '%s' not found", requestId, serviceName);

  //
  // No entity/attribute mapping -> nothing to store. Still free the instance.
  //
  if ((serviceP->entityId == NULL) || (serviceP->attributeName == NULL))
  {
    KT_T(StDdsService, "Service '%s' has no entity/attribute mapping - reply not stored", serviceName);
    ddsInstanceFree(dsiP);
    return;
  }

  //
  // Initialize orionldState for this DDS callback thread (same pattern as
  // ddsNotification.cpp). Everything we build below lives in orionldState.kjsonP.
  //
  orionldStateInit(NULL);

  //
  // Parse the enabler payload and tease out the metadata fields.
  //
  KjNode* replyTree = kjParse(orionldState.kjsonP, (char*) json);
  if (replyTree == NULL)
  {
    KT_W("Error parsing service reply JSON from DDS: '%s'", json);
    ddsInstanceFree(dsiP);
    return;
  }

  const char* participantId = NULL;
  const char* ddsDataType   = NULL;
  const char* xId           = NULL;
  KjNode*     replyPayload  = extractReplyMetadata(replyTree, &participantId, &ddsDataType, &xId);

  if (replyPayload == NULL)
  {
    // Couldn't parse the expected envelope shape - fall back to using the raw
    // tree as the reply value. Better to store something than nothing.
    KT_W("Reply for service '%s' didn't match the expected DDS envelope - storing raw payload", serviceName);
    replyPayload = replyTree;
  }

  //
  // Build the patch tree:
  //   { <attrLongName>: { type: Property, request: {...}, reply: {...} } }
  //
  char*   attrLongName = orionldAttributeExpand(orionldState.contextP, serviceP->attributeName, true, NULL);
  KjNode* entityBody   = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrBody     = kjObject(orionldState.kjsonP, attrLongName);
  KjNode* attrType     = kjString(orionldState.kjsonP, "type", "Property");

  kjChildAdd(attrBody, attrType);
  kjChildAdd(entityBody, attrBody);

  //
  // request sub-attribute - clone the stored request tree into the current
  // kjson context so it can be safely embedded.
  //
  // ddsDataType is taken from the broker's known service config
  // (serviceP->requestType) - that's the request side's type name. The other
  // two DDS-specific identifiers (xId, participantId) are not exposed by the
  // eProsima enabler at request submission time and are left out.
  //
  if ((dsiP != NULL) && (dsiP->requestTree != NULL))
  {
    KjNode* clonedRequest = kjClone(orionldState.kjsonP, dsiP->requestTree);
    KjNode* requestSub    = buildSubAttribute("request",
                                              clonedRequest,
                                              requestId,
                                              NULL,                  // xId           - not exposed by enabler
                                              NULL,                  // participantId - not exposed by enabler
                                              serviceP->requestType, // ddsDataType   - from broker config
                                              dsiP->publishedAt);
    kjChildAdd(attrBody, requestSub);
  }

  //
  // reply sub-attribute
  //
  KjNode* replySub = buildSubAttribute("reply",
                                       replyPayload,
                                       requestId,
                                       xId,
                                       participantId,
                                       ddsDataType,
                                       publishTime);
  kjChildAdd(attrBody, replySub);

  //
  // Set up orionldState for merge-patch and call orionldPatchEntity2.
  // ddsSample = true blocks the re-publish path in ddsPublishAttributes/Attribute,
  // preventing the infinite loop that used to occur on consecutive requests.
  //
  char* expandedType = orionldContextItemExpand(orionldState.contextP, serviceP->entityType, true, NULL);

  orionldState.requestTree    = entityBody;
  orionldState.wildcard[0]    = serviceP->entityId;
  orionldState.tenantP        = &tenant0;
  orionldState.ddsSample      = true;
  orionldState.ddsPublishTime = publishTime;
  orionldState.apiVersion     = API_VERSION_NGSILD_V1;
  orionldState.uriParams.type = expandedType;
  orionldState.serviceP       = serviceLookupByServiceRoutine(orionldPatchEntity2, HTTP_PATCH);

  KT_T(StDdsService, "Merge-patching entity '%s' with request/reply for attribute '%s'", serviceP->entityId, serviceP->attributeName);
  orionldPatchEntity2();

  //
  // Release the per-instance kalloc/kjson buffer pair now that the cloned
  // request tree is no longer needed.
  //
  ddsInstanceFree(dsiP);

  //
  // Cleanup (same as ddsNotification.cpp)
  //
  void* con_cls;
  extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);

  requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);
}
