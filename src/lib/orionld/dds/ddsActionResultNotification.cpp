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

#include "ddsenabler_participants/rpc/RpcTypes.hpp"              // UUID

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjParse.h"                                       // kjParse
}

#include "orionld/types/DdsAction.h"                             // DdsAction, DdsActionGoal
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/ddsActionLookup.h"                         // ddsActionLookup
#include "orionld/dds/ddsActionSubAttributeUpdate.h"             // ddsActionSubAttributeUpdate
#include "orionld/dds/ddsReplyBuild.h"                           // ddsReplyExtractMetadata
#include "orionld/dds/ddsActionResultNotification.h"             // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionResultNotification -
//
// Called by the DDS Enabler when a result arrives for an action goal.
// The result is stored as a "ddsActionResult" sub-attribute.
//
void ddsActionResultNotification
(
  const char*                                          actionName,
  const char*                                          json,
  const eprosima::ddsenabler::participants::UUID&      goalId,
  int64_t                                              publishTime
)
{
  KT_T(StDdsAction, "Got an Action Result Notification (action: '%s'): '%s'", actionName, json);

  DdsAction* actionP = ddsActionLookup(actionName);
  if (actionP == NULL)
  {
    KT_W("Action '%s' not found", actionName);
    return;
  }

  if (actionP->entityId == NULL || actionP->attributeName == NULL)
  {
    KT_T(StDdsAction, "Action '%s' has no entity/attribute mapping - result not stored", actionName);
    return;
  }

  // publishTime arrives from the enabler in nanoseconds since epoch; reduce to
  // seconds so it matches request-side timestamps recorded with time(NULL).
  publishTime /= 1000000000LL;

  orionldStateInit(NULL);
  KjNode* resultTree = kjParse(orionldState.kjsonP, (char*) json);
  if (resultTree == NULL)
  {
    KT_W("Error parsing action result JSON: '%s'", json);
    return;
  }

  //
  // If the enabler's payload carries the same envelope shape as a service
  // reply ({ "id": <participantId>, "rr/<ddsDataType>": { "type": ..., "data": { "<handle>": <payload> } } })
  // tease it out so we can populate the envelope sub-Properties.
  //
  const char* participantId    = NULL;
  const char* ddsDataType      = NULL;
  const char* instanceHandleId = NULL;
  KjNode*     resultPayload    = ddsReplyExtractMetadata(resultTree, &participantId, &ddsDataType, &instanceHandleId);

  if (resultPayload == NULL)
    resultPayload = resultTree;  // no envelope — store the raw tree as value

  //
  // Look up the in-flight goal record (for lazy-create + instance tracking).
  // Note: goal freeing has moved to ddsActionStatusNotification on terminal
  // status, so we still find a valid goalP here.
  //
  DdsActionGoal* goalP = ddsActionGoalLookup(actionP, goalId.data());

  ddsActionSubAttributeUpdate(actionP->entityId,
                              actionP->entityType,
                              actionP->attributeName,
                              "ddsActionResult",
                              resultPayload,
                              goalId,
                              goalP,
                              instanceHandleId,
                              participantId,
                              ddsDataType,
                              publishTime);
}
