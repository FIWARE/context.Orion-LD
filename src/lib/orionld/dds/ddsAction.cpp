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
#include <stdlib.h>                                              // malloc
#include <string.h>                                              // memcpy

#include "ddsenabler/DDSEnabler.hpp"                             // DDSEnabler::send_action_goal
#include "ddsenabler_participants/rpc/RpcTypes.hpp"              // UUID

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/types/DdsAction.h"                             // DdsAction, DdsActionGoal
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler
#include "orionld/dds/ddsActionBuild.h"                          // ddsActionGoalDatasetId
#include "orionld/dds/ddsActionSubscription.h"                   // ddsActionSubscriptionCreate
#include "orionld/dds/ddsAction.h"                               // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionGoalSend -
//
void ddsActionGoalSend(DdsAction* actionP, KjNode* attributeValueP, const char* endpointUri)
{
  int   jsonLen = kjFastRenderSize(attributeValueP);
  char* json    = kaAlloc(&orionldState.kalloc, jsonLen + 20);

  kjFastRender(attributeValueP, json);

  KT_T(StDdsAction, "Sending action goal for '%s'", actionP->name);

  //
  // send_action_goal fills in the goalId (UUID)
  //
  eprosima::ddsenabler::participants::UUID goalId;

  KT_T(StDdsAction, "action name: '%s', json: '%s'", actionP->name, json);
  bool result = ddsEnabler->send_action_goal(actionP->name, json, goalId);

  if (result)
  {
    //
    // Track the goal. requestJson is preserved (libc strdup of the rendered
    // payload) for lazy creation of the per-goal datasetId instance once the
    // first non-terminal status/feedback notification arrives - see
    // ddsActionSubAttributeUpdate. calloc zero-inits instanceCreated.
    //
    DdsActionGoal* gP = (DdsActionGoal*) calloc(1, sizeof(DdsActionGoal));

    memcpy(gP->goalId, goalId.data(), 16);

    gP->requestJson = strdup(json);
    gP->next        = actionP->goals;
    actionP->goals  = gP;

    //
    // If the PATCH carried an 'endpoint' sub-attribute, create a temporary
    // (cache-only) subscription so the initiator gets this goal's feedback /
    // result / status. Torn down on the goal's terminal status. A failure to
    // create the subscription must NOT fail the goal - gP->subscriptionId
    // simply stays NULL.
    //
    if (endpointUri != NULL)
    {
      // Scope the temp sub to THIS goal's datasetId so its notifications carry
      // the goal's own feedback/result/status instance (not the default).
      char goalDatasetId[48];
      ddsActionGoalDatasetId(goalId, goalDatasetId);
      gP->subscriptionId = ddsActionSubscriptionCreate(actionP->entityId, actionP->entityType, actionP->attributeName, endpointUri, goalDatasetId);
    }

    KT_T(StDdsAction, "Sent action goal for '%s'", actionP->name);
  }
  else
    KT_W("Failed to send action goal for '%s'", actionP->name);
}
