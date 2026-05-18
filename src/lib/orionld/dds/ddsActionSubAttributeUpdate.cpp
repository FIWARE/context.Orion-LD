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
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd, kjString
#include "kjson/kjParse.h"                                       // kjParse
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/mongoc/mongocDatasetInstanceOps.h"              // mongocDatasetInstancePush, mongocDatasetSubAttrSet
#include "orionld/dds/ddsActionBuild.h"                          // ddsActionBuildSubAttribute, ddsActionGoalDatasetId
#include "orionld/dds/ddsActionLifecycleNotify.h"                // ddsActionLifecycleNotify
#include "orionld/dds/ddsActionSubAttributeUpdate.h"             // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionSubAttributeUpdate -
//
// Materialise an envelope-rich sub-attribute (ddsActionFeedback /
// ddsActionResult / ddsActionStatus) onto the per-goal datasetId instance of
// an action-tied attribute.
//
// Storage shape inside the entity's @datasets array:
//
//   "@datasets": {
//     "<attrEqName>": [
//       {
//         "type": "Property",
//         "datasetId": "urn:goal:<uuid>",
//         "value": <original goal request payload>,
//         "<subAttributeName>": { ... envelope ... },
//         "createdAt": ..., "modifiedAt": ...
//       }
//     ]
//   }
//
// Lazy create: when goalP->instanceCreated is false, write the full instance
// via $push (single mongo round-trip). Subsequent notifications $set the
// envelope sub-attr inside the existing instance via arrayFilters.
//
// Goes direct to mongo — bypasses orionldPatchEntity2 (its body-level
// datasetId support is a PoC with several open issues). Consequence: no
// NGSI-LD subscription dispatch (intentional for internal DDS plumbing),
// no TRoE recording for these writes (the per-goal lifecycle would be
// noisy in TRoE anyway).
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

  // Mongo connection needs a tenant.
  if (orionldState.tenantP == NULL)
    orionldState.tenantP = &tenant0;

  (void) entityType;  // not needed for direct mongo writes

  char* attrLongName = orionldAttributeExpand(orionldState.contextP, attributeName, true, NULL);

  KjNode* envelope = ddsActionBuildSubAttribute(subAttributeName,
                                                subAttributeValue,
                                                goalId,
                                                instanceHandleId,
                                                participantId,
                                                ddsDataType,
                                                publishTime);

  char datasetIdStr[48];
  ddsActionGoalDatasetId(goalId, datasetIdStr);

  bool isFirstPatch = (goalP != NULL) && (goalP->instanceCreated == false);

  if (isFirstPatch)
  {
    //
    // Lazy create: $push the full instance (type/datasetId/value/envelope).
    //
    KjNode* instance      = kjObject(orionldState.kjsonP, NULL);
    KjNode* typeNode      = kjString(orionldState.kjsonP, "type",      "Property");
    KjNode* datasetIdNode = kjString(orionldState.kjsonP, "datasetId", datasetIdStr);

    kjChildAdd(instance, typeNode);
    kjChildAdd(instance, datasetIdNode);

    // Value from the original goal request (re-parsed). If parse fails we
    // fall back to a placeholder string so the instance is still well-formed.
    if (goalP->requestJson != NULL)
    {
      KjNode* valueTree = kjParse(orionldState.kjsonP, goalP->requestJson);
      if (valueTree != NULL)
      {
        valueTree->name = (char*) "value";
        kjChildAdd(instance, valueTree);
      }
      else
      {
        KT_W("Failed to re-parse goal request JSON for lazy create: '%s'", goalP->requestJson);
        kjChildAdd(instance, kjString(orionldState.kjsonP, "value", goalP->requestJson));
      }
    }
    else
      kjChildAdd(instance, kjString(orionldState.kjsonP, "value", ""));

    kjChildAdd(instance, envelope);

    KT_T(StDdsAction, "Lazy create per-goal instance for entity '%s' attr '%s' (datasetId %s, sub '%s')",
         entityId, attributeName, datasetIdStr, subAttributeName);

    if (mongocDatasetInstancePush(entityId, attrLongName, instance) == true)
    {
      goalP->instanceCreated = true;
      ddsActionLifecycleNotify(entityId, entityType, attrLongName, instance);
    }
  }
  else
  {
    //
    // Surgical: $set the envelope sub-attribute inside the existing instance.
    //
    KT_T(StDdsAction, "Surgical sub-attr set on entity '%s' attr '%s' (datasetId %s, sub '%s')",
         entityId, attributeName, datasetIdStr, subAttributeName);

    if (mongocDatasetSubAttrSet(entityId, attrLongName, datasetIdStr, subAttributeName, envelope) == true)
    {
      // TRoE payload: an attribute fragment carrying just the changed sub-attr.
      KjNode* troePayload = kjObject(orionldState.kjsonP, NULL);
      kjChildAdd(troePayload, kjString(orionldState.kjsonP, "type", "Property"));
      kjChildAdd(troePayload, envelope);  // envelope is the sub-attribute we set
      ddsActionLifecycleNotify(entityId, entityType, attrLongName, troePayload);
    }
  }
}
