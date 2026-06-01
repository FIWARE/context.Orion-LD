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
#include "microhttpd.h"                                          // MHD_RequestTerminationCode

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd, kjString
#include "kjson/kjParse.h"                                       // kjParse
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/serviceRoutines/orionldPatchEntity2.h"         // orionldPatchEntity2
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/dds/ddsActionBuild.h"                          // ddsActionBuildSubAttribute, ddsActionGoalDatasetId
#include "orionld/dds/ddsActionSubAttributeUpdate.h"             // Own interface

extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);



// -----------------------------------------------------------------------------
//
// ddsActionSubAttributeUpdate -
//
// Materialise an envelope-rich sub-attribute (ddsActionFeedback /
// ddsActionResult / ddsActionStatus) onto the per-goal datasetId instance of
// an action-tied attribute, by issuing a PATCH /entities/{id} through
// orionldPatchEntity2.
//
// Resulting instance shape inside the entity's @datasets array:
//
//   "@datasets": {
//     "<attrEqName>": [
//       {
//         "type": "Property",
//         "datasetId": "urn:goal:<uuid>",
//         "value": <original goal request payload>,
//         "ddsActionStatus":   { ... envelope ... },
//         "ddsActionFeedback": { ... },
//         "ddsActionResult":   { ... },
//         ...
//       }
//     ]
//   }
//
// First call for a goal creates the instance (orionldPatchEntity2 +
// dbModelFromApiAttributeDatasetArray + the @datasets-merge logic in
// orionldPatchEntity2 upserts by datasetId per NGSI-LD merge semantics).
// Subsequent calls patch the same datasetId - the merge code at the
// PATCH level preserves sub-attributes the patch doesn't touch.
//
// Going through orionldPatchEntity2 means subscription dispatch and
// (when -troe is on) TRoE recording happen automatically via the standard
// pipeline - no per-call manual wiring needed.
//
void ddsActionSubAttributeUpdate
(
  const char*                                       entityId,
  const char*                                       entityType,
  const char*                                       attributeName,
  const char*                                       subAttributeName,
  KjNode*                                           subAttributeValue,
  const eprosima::ddsenabler::participants::UUID&   goalId,
  DdsActionGoal*                                    goalP,
  const char*                                       instanceHandleId,
  const char*                                       participantId,
  const char*                                       ddsDataType,
  int64_t                                           publishTime
)
{
  // The caller (status/feedback/result notification handler) has already
  // run orionldStateInit and built subAttributeValue in orionldState.kalloc.
  // Do NOT call orionldStateInit here - it would invalidate that arena.

  char* attrLongName = orionldAttributeExpand(orionldState.contextP, attributeName, true, NULL);

  //
  // Envelope sub-attribute (type/value/publishedAt/...)
  //
  KjNode* envelope = ddsActionBuildSubAttribute(subAttributeName,
                                                subAttributeValue,
                                                instanceHandleId,
                                                participantId,
                                                ddsDataType,
                                                publishTime);

  //
  // datasetId for the action attribute instance — keyed by goalId
  //
  char datasetIdStr[48];
  ddsActionGoalDatasetId(goalId, datasetIdStr);

  //
  // Build the patch body:
  //   { "<attrLongName>": { type, datasetId, value, "<subAttr>": <envelope> } }
  //
  // The value is the original goal request payload (re-parsed from
  // goalP->requestJson). It's included on every patch so the
  // PATCH-level merge code's "new instance" path has a value to write,
  // and the "matching datasetId" path harmlessly re-asserts the same
  // value (the merge preserves unmentioned sub-attrs).
  //
  KjNode* entityBody    = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrBody      = kjObject(orionldState.kjsonP, attrLongName);
  KjNode* attrTypeNode  = kjString(orionldState.kjsonP, "type",      "Property");
  KjNode* datasetIdNode = kjString(orionldState.kjsonP, "datasetId", datasetIdStr);

  kjChildAdd(attrBody, attrTypeNode);
  kjChildAdd(attrBody, datasetIdNode);

  if ((goalP != NULL) && (goalP->requestJson != NULL))
  {
    // kjParse mutates its input buffer (chops the JSON into tokens with NULs)
    // so a long-lived requestJson would be destroyed on the first parse. Copy
    // into the per-request kalloc arena before parsing.
    char*   jsonCopy  = kaStrdup(&orionldState.kalloc, goalP->requestJson);
    KjNode* valueTree = (jsonCopy != NULL) ? kjParse(orionldState.kjsonP, jsonCopy) : NULL;
    if (valueTree != NULL)
    {
      valueTree->name = (char*) "value";
      kjChildAdd(attrBody, valueTree);
    }
    else
    {
      KT_W("Failed to re-parse goal request JSON: '%s'", goalP->requestJson);
      kjChildAdd(attrBody, kjString(orionldState.kjsonP, "value", goalP->requestJson));
    }
  }

  kjChildAdd(attrBody, envelope);
  kjChildAdd(entityBody, attrBody);

  char* expandedType = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);

  orionldState.requestTree         = entityBody;
  orionldState.wildcard[0]         = (char*) entityId;
  orionldState.tenantP             = &tenant0;
  orionldState.ddsSample           = true;
  orionldState.ddsPublishTime      = publishTime;
  orionldState.apiVersion          = API_VERSION_NGSILD_V1;
  orionldState.uriParams.type      = expandedType;
  orionldState.serviceP            = serviceLookupByServiceRoutine(orionldPatchEntity2, HTTP_PATCH);

  KT_T(StDdsAction, "PATCH /entities/%s with %s envelope for attr %s (datasetId %s)",
       entityId, subAttributeName, attributeName, datasetIdStr);
  (void) orionldPatchEntity2();

  // Drives orionldAlterationsTreat (subscription dispatch) + the service
  // routine's TRoE routine via the standard rest.cpp pipeline.
  void* con_cls = NULL;
  requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);
}
