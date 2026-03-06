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
#include "orionld/dds/ddsActionResultNotification.h"             // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionResultNotificationFunc -
//
// Called by the DDS Enabler when a result arrives for an action goal.
// The result is stored as a "ddsActionResult" sub-attribute.
//
void ddsActionResultNotificationFunc
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

  //
  // Remove the matching goal from the goals list
  //
  DdsActionGoal* prev = NULL;
  for (DdsActionGoal* gP = actionP->goals; gP != NULL; gP = gP->next)
  {
    if (memcmp(gP->goalId, goalId.data(), 16) == 0)
    {
      if (prev != NULL)
        prev->next = gP->next;
      else
        actionP->goals = gP->next;
      free(gP);
      break;
    }
    prev = gP;
  }

  orionldStateInit(NULL);
  KjNode* resultTree = kjParse(orionldState.kjsonP, (char*) json);
  if (resultTree == NULL)
  {
    KT_W("Error parsing action result JSON: '%s'", json);
    return;
  }

  ddsActionSubAttributeUpdate(actionP->entityId, actionP->entityType, actionP->attributeName, "ddsActionResult", resultTree, publishTime);
}
