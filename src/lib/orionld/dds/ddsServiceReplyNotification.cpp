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

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd
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
// ddsServiceReplyNotification -
//
// Called by the DDS Enabler when a reply arrives for a service request
// that was previously sent by the broker (acting as client).
//
// The reply data is stored as a "ddsServiceResponse" sub-attribute
// of the service's mapped NGSI-LD attribute.
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

  DdsService* serviceP = ddsServiceLookup(serviceName);
  if (serviceP == NULL)
  {
    KT_W("Service '%s' not found", serviceName);
    return;
  }

  //
  // Find and remove the instance tracking this request
  //
  DdsServiceInstance* prev = NULL;
  bool found = false;

  for (DdsServiceInstance* dsiP = serviceP->instances; dsiP != NULL; dsiP = dsiP->next)
  {
    if (dsiP->requestId == requestId)
    {
      if (prev != NULL)
        prev->next = dsiP->next;
      else
        serviceP->instances = dsiP->next;

      free(dsiP);
      found = true;
      break;
    }

    prev = dsiP;
  }

  if (found == false)
    KT_W("Instance '%llu' of service '%s' not found", requestId, serviceName);

  //
  // Update the entity attribute with the reply data
  //
  if (serviceP->entityId == NULL || serviceP->attributeName == NULL)
  {
    KT_T(StDdsService, "Service '%s' has no entity/attribute mapping - reply not stored", serviceName);
    return;
  }

  //
  // Initialize orionldState for this DDS callback thread
  // (same pattern as ddsNotification.cpp)
  //
  orionldStateInit(NULL);

  KjNode* replyTree = kjParse(orionldState.kjsonP, (char*) json);
  if (replyTree == NULL)
  {
    KT_W("Error parsing service reply JSON from DDS: '%s'", json);
    return;
  }

  //
  // Use merge-patch (orionldPatchEntity2) to add/update the "ddsServiceResponse"
  // sub-attribute without replacing the existing attribute value.
  //
  // The request tree for merge-patch at entity level:
  //   { "<attributeName>": { "ddsServiceResponse": { "type": "Property", "value": <reply> } } }
  //
  char* attrLongName = orionldAttributeExpand(orionldState.contextP, serviceP->attributeName, true, NULL);

  KjNode* entityBody    = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrBody      = kjObject(orionldState.kjsonP, attrLongName);
  KjNode* subAttr       = kjObject(orionldState.kjsonP, "ddsServiceResponse");
  KjNode* subAttrType   = kjString(orionldState.kjsonP, "type", "Property");

  // The reply tree becomes the value of ddsServiceResponse
  replyTree->name = (char*) "value";

  kjChildAdd(subAttr, subAttrType);
  kjChildAdd(subAttr, replyTree);
  kjChildAdd(attrBody, subAttr);
  kjChildAdd(entityBody, attrBody);

  //
  // Set up orionldState for merge-patch
  //
  char* expandedType = orionldContextItemExpand(orionldState.contextP, serviceP->entityType, true, NULL);

  orionldState.requestTree         = entityBody;
  orionldState.wildcard[0]         = serviceP->entityId;
  orionldState.tenantP             = &tenant0;
  orionldState.ddsSample           = true;
  orionldState.ddsPublishTime      = publishTime;
  orionldState.apiVersion          = API_VERSION_NGSILD_V1;
  orionldState.uriParams.type      = expandedType;
  orionldState.serviceP            = serviceLookupByServiceRoutine(orionldPatchEntity2, HTTP_PATCH);

  KT_T(StDdsService, "Merge-patching entity '%s' with service reply for attribute '%s'", serviceP->entityId, serviceP->attributeName);
  orionldPatchEntity2();

  //
  // Cleanup (same as ddsNotification.cpp)
  //
  void* con_cls;
  extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);

  requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);
}
