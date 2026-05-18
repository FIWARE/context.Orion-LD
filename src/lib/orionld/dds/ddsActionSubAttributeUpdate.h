#ifndef SRC_LIB_ORIONLD_DDS_DDSACTIONSUBATTRIBUTEUPDATE_H_
#define SRC_LIB_ORIONLD_DDS_DDSACTIONSUBATTRIBUTEUPDATE_H_

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
#include <stdint.h>                                              // int64_t

#include "ddsenabler_participants/rpc/RpcTypes.hpp"              // UUID

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/DdsAction.h"                             // DdsActionGoal



// -----------------------------------------------------------------------------
//
// ddsActionSubAttributeUpdate -
//
// Merge-patch a single envelope-rich sub-attribute (ddsActionFeedback /
// ddsActionResult / ddsActionStatus) onto the action-tied attribute of an
// entity, keyed by datasetId=urn:goal:<goalId>.
//
// The resulting attribute instance has the form:
//
//   "<attrLongName>": {
//     "type": "Property",
//     "datasetId": "urn:goal:<uuid>",
//     "<subAttributeName>": {
//        "type": "Property",
//        "value": <subAttributeValue>,
//        "goalId":      { "type": "Property", "value": "<uuid>" },
//        "publishedAt": { "type": "Property", "value": <secs> }
//        [+ optional ddsDataType / instanceHandleId / participantId]
//     }
//   }
//
// Passing subAttributeValue = NULL produces an envelope with no value (used
// by status notifications whose payload is split across statusCode/message
// rendered inside the caller).
//
// goalP is the in-flight goal tracker. On the first call for a goal (with
// goalP->instanceCreated == false), the attribute's "value" is initialised
// from goalP->requestJson and goalP->instanceCreated is set to true. On
// subsequent calls (or when goalP is NULL because the goal record has been
// freed), the patch carries only the envelope sub-attribute - the per-goal
// instance already exists.
//
extern void ddsActionSubAttributeUpdate
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
);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONSUBATTRIBUTEUPDATE_H_
