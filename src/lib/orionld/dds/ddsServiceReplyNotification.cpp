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
#include "kalloc/kaStrdup.h"                                     // kaStrdup
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
#include "orionld/dds/ddsInstance.h"                             // ddsInstancePop, ddsInstanceFree
#include "orionld/dds/ddsReplyBuild.h"                           // ddsReplyBuildSubAttribute, ddsReplyExtractMetadata
#include "orionld/dds/ddsServiceReplyNotification.h"             // Own interface



// (Sub-attribute helpers - stringPropertyNode, integerPropertyNode,
// extractReplyMetadata, buildSubAttribute - moved to ddsReplyBuild.{h,cpp}
// so the ddsSync path can share them.)



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
//       "value": <actual reply payload (instance-handle-keyed value, unwrapped)>,
//       "requestId":        { "type": "Property", "value": <id> },
//       "instanceHandleId": { "type": "Property", "value": <instanceHandleId> },
//       "participantId":    { "type": "Property", "value": <participantId> },
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
  {
    // Happens legitimately if the PATCH thread (in ddsSync mode) already
    // timed out and popped the instance itself. No state to restore.
    KT_W("Instance '%llu' of service '%s' not found (ddsSync timeout raced?)", requestId, serviceName);
    return;
  }

  //
  // ddsSync path: the request thread is blocked on dsiP->cv. Parse the reply
  // into the instance's own kalloc so it survives past this callback, store
  // the envelope metadata, then signal. The request thread performs the
  // merge-patch itself - this thread returns immediately.
  //
  if (dsiP->syncMode == true)
  {
    KjNode*     replyTreeSync    = kjParse(&dsiP->kjson, (char*) json);
    const char* participantId    = NULL;
    const char* ddsDataType      = NULL;
    const char* instanceHandleId = NULL;
    KjNode*     replyPayload     = (replyTreeSync != NULL)
                                   ? ddsReplyExtractMetadata(replyTreeSync, &participantId, &ddsDataType, &instanceHandleId)
                                   : NULL;

    if (replyPayload == NULL)
    {
      KT_W("Reply for service '%s' (sync) didn't match the expected envelope - storing raw tree", serviceName);
      replyPayload = replyTreeSync;
    }

    pthread_mutex_lock(&dsiP->mtx);
    dsiP->replyTree        = replyPayload;
    dsiP->replyPublishedAt = publishTime;
    dsiP->ddsDataType      = (ddsDataType      != NULL) ? kaStrdup(&dsiP->kalloc, ddsDataType)      : NULL;
    dsiP->participantId    = (participantId    != NULL) ? kaStrdup(&dsiP->kalloc, participantId)    : NULL;
    dsiP->instanceHandleId = (instanceHandleId != NULL) ? kaStrdup(&dsiP->kalloc, instanceHandleId) : NULL;
    dsiP->replyReceived    = true;
    pthread_cond_signal(&dsiP->cv);
    pthread_mutex_unlock(&dsiP->mtx);

    // Do NOT free - the request thread consumes replyTree and frees via
    // ddsInstanceFree once it's done merging into the entity tree.
    return;
  }

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

  const char* participantId    = NULL;
  const char* ddsDataType      = NULL;
  const char* instanceHandleId = NULL;
  KjNode*     replyPayload     = ddsReplyExtractMetadata(replyTree, &participantId, &ddsDataType, &instanceHandleId);

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
  // two DDS-specific identifiers (instanceHandleId, participantId) are not
  // exposed by the eProsima enabler at request submission time and are left out.
  //
  if ((dsiP != NULL) && (dsiP->requestTree != NULL))
  {
    KjNode* clonedRequest = kjClone(orionldState.kjsonP, dsiP->requestTree);
    KjNode* requestSub    = ddsReplyBuildSubAttribute("request",
                                              clonedRequest,
                                              requestId,
                                              NULL,                  // instanceHandleId - not exposed by enabler
                                              NULL,                  // participantId    - not exposed by enabler
                                              serviceP->requestType, // ddsDataType   - from broker config
                                              dsiP->publishedAt);
    kjChildAdd(attrBody, requestSub);
  }

  //
  // reply sub-attribute
  //
  KjNode* replySub = ddsReplyBuildSubAttribute("reply",
                                       replyPayload,
                                       requestId,
                                       instanceHandleId,
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
