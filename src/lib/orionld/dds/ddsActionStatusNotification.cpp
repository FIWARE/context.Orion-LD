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

#include "ddsenabler_participants/rpc/RpcTypes.hpp"              // UUID, StatusCode

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjInteger
}

#include "orionld/types/DdsAction.h"                             // DdsAction
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/ddsActionLookup.h"                         // ddsActionLookup
#include "orionld/dds/ddsActionSubAttributeUpdate.h"             // ddsActionSubAttributeUpdate
#include "orionld/dds/ddsActionInstanceDelete.h"                 // ddsActionInstanceDelete
#include "orionld/dds/ddsActionBuild.h"                          // ddsActionStatusCodeToString, ddsActionGoalDatasetId
#include "orionld/dds/ddsActionSubscription.h"                   // ddsActionSubscriptionDelete
#include "orionld/dds/ddsActionStatusNotification.h"             // Own interface



// -----------------------------------------------------------------------------
//
// statusIsTerminal / statusIsSuccess
//
static bool statusIsTerminal(eprosima::ddsenabler::participants::StatusCode c)
{
  using SC = eprosima::ddsenabler::participants::StatusCode;
  return (c == SC::SUCCEEDED) ||
         (c == SC::CANCELED)  ||
         (c == SC::ABORTED)   ||
         (c == SC::REJECTED)  ||
         (c == SC::TIMEOUT)   ||
         (c == SC::FAILED)    ||
         (c == SC::CANCEL_REQUEST_FAILED);
}

static bool statusIsSuccess(eprosima::ddsenabler::participants::StatusCode c)
{
  return c == eprosima::ddsenabler::participants::StatusCode::SUCCEEDED;
}



// -----------------------------------------------------------------------------
//
// goalUnlinkAndFree - remove goalP from actionP->goals and free it
//
static void goalUnlinkAndFree(DdsAction* actionP, DdsActionGoal* goalP)
{
  if ((actionP == NULL) || (goalP == NULL))
    return;

  DdsActionGoal* prev = NULL;
  for (DdsActionGoal* gP = actionP->goals; gP != NULL; gP = gP->next)
  {
    if (gP == goalP)
    {
      if (prev != NULL)
        prev->next = gP->next;
      else
        actionP->goals = gP->next;
      free(gP->requestJson);
      if (gP->subscriptionId != NULL)
        free(gP->subscriptionId);
      free(gP);
      return;
    }
    prev = gP;
  }
}



// -----------------------------------------------------------------------------
//
// ddsActionStatusNotification -
//
void ddsActionStatusNotification
(
  const char*                                              actionName,
  const eprosima::ddsenabler::participants::UUID&          goalId,
  eprosima::ddsenabler::participants::StatusCode           statusCode,
  const char*                                              statusMessage,
  int64_t                                                  publishTime
)
{
  KT_T(StDdsAction, "Got an Action Status Notification (action: %s, status %d): %s", actionName, (int) statusCode, statusMessage);

  DdsAction* actionP = ddsActionLookup(actionName);
  if (actionP == NULL)
  {
    KT_W("Action '%s' not found", actionName);
    return;
  }

  if (actionP->entityId == NULL || actionP->attributeName == NULL)
    return;

  publishTime /= 1000000000LL;

  //
  // Build a JSON object for the status: { "code": "executing", "message": "..." }
  // This becomes the envelope's value; goalId/publishedAt are added by
  // ddsActionSubAttributeUpdate.
  //
  orionldStateInit(NULL);

  KjNode* statusTree = kjObject(orionldState.kjsonP, NULL);
  KjNode* codeNode   = kjString(orionldState.kjsonP, "code", ddsActionStatusCodeToString(statusCode));
  KjNode* msgNode    = kjString(orionldState.kjsonP, "message", (statusMessage != NULL) ? statusMessage : "");

  kjChildAdd(statusTree, codeNode);
  kjChildAdd(statusTree, msgNode);

  DdsActionGoal* goalP            = ddsActionGoalLookup(actionP, goalId.data());
  bool           instanceCreated  = (goalP != NULL) ? goalP->instanceCreated : false;
  bool           isTerminal       = statusIsTerminal(statusCode);
  bool           isFailure        = isTerminal && !statusIsSuccess(statusCode);

  //
  // On a terminal failure that arrives before any instance has been
  // materialised (e.g. immediate REJECTED): skip the envelope write
  // altogether. Nothing was created, nothing to clean up beyond freeing the
  // goal record.
  //
  if (isFailure && (instanceCreated == false))
  {
    KT_T(StDdsAction, "Terminal failure (%s) before lazy create - dropping goal without DB write",
         ddsActionStatusCodeToString(statusCode));
    if ((goalP != NULL) && (goalP->subscriptionId != NULL))
      ddsActionSubscriptionDelete(goalP->subscriptionId);
    goalUnlinkAndFree(actionP, goalP);
    return;
  }

  ddsActionSubAttributeUpdate(actionP->entityId,
                              actionP->entityType,
                              actionP->attributeName,
                              "ddsActionStatus",
                              statusTree,
                              goalId,
                              goalP,
                              /*instanceHandleId*/ NULL,
                              /*participantId*/    NULL,
                              /*ddsDataType*/      NULL,
                              publishTime);

  //
  // Terminal handling:
  //   - succeeded: keep the per-goal instance (final envelope is the record
  //     of completion). DON'T free the goal tracker here - in ROS2 the
  //     result notification typically arrives shortly AFTER the succeeded
  //     status, and that handler still needs goalP->requestJson to stamp
  //     the per-goal instance's "value" field. ddsActionResultNotification
  //     will free the tracker.
  //   - any other terminal (canceled/aborted/rejected/timeout/failed/...):
  //     no result is expected. Pull the per-goal instance from the DB
  //     (won't grow forever) and free the tracker here. Subscription
  //     dispatch fires for the pull; no DDS goal-cancel hook (we go via
  //     direct mongo, not orionldDeleteAttribute).
  //
  if (isTerminal && isFailure)
  {
    // Deliver-then-teardown: the status envelope PATCH above already fired the
    // final notification to the temp subscription (if any). Remove the
    // subscription BEFORE pulling the instance so the consumer doesn't also
    // get a notification for the instance deletion.
    if ((goalP != NULL) && (goalP->subscriptionId != NULL))
      ddsActionSubscriptionDelete(goalP->subscriptionId);

    // Sole in-flight goal -> its instance is the last datasetId instance, so the
    // whole attribute is removed (re-created by the next goal-triggering PATCH).
    bool lastInstance = (actionP->goals == goalP) && (goalP->next == NULL);

    char datasetIdStr[48];
    ddsActionGoalDatasetId(goalId, datasetIdStr);
    ddsActionInstanceDelete(actionP->entityId, actionP->entityType, actionP->attributeName, datasetIdStr, lastInstance);
    goalUnlinkAndFree(actionP, goalP);
  }
}
