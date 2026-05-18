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
#include <string.h>                                              // memcmp

#include "ddsenabler/DDSEnabler.hpp"                             // cancel_action_goal
#include "ddsenabler_participants/rpc/RpcTypes.hpp"              // UUID

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages
}

#include "orionld/types/DdsAction.h"                             // DdsAction, DdsActionGoal
#include "orionld/common/orionldState.h"                         // ddsSupport
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler
#include "orionld/dds/ddsActionLookup.h"                         // ddsActionLookupByAttributeName
#include "orionld/dds/ddsActionBuild.h"                          // ddsActionDatasetIdToUuid
#include "orionld/dds/ddsActionGoalCancel.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionGoalCancelIfMapped -
//
bool ddsActionGoalCancelIfMapped(const char* attrShortName, const char* datasetId)
{
  if (ddsSupport == false)
    return false;
  if ((attrShortName == NULL) || (datasetId == NULL))
    return false;

  DdsAction* actionP = ddsActionLookupByAttributeName(attrShortName);
  if (actionP == NULL)
    return false;

  eprosima::ddsenabler::participants::UUID goalId;
  if (ddsActionDatasetIdToUuid(datasetId, goalId) == false)
  {
    KT_W("Action '%s' DELETE datasetId '%s' isn't a valid 'urn:goal:<uuid>' - cancel skipped", actionP->name, datasetId);
    return false;
  }

  KT_T(StDdsAction, "Sending cancel for action '%s', goal %s", actionP->name, datasetId);

  if (ddsEnabler == nullptr)
  {
    KT_W("DDS Enabler not initialized - cancel for action '%s' skipped", actionP->name);
    return false;
  }

  bool ok = ddsEnabler->cancel_action_goal(actionP->name, goalId);
  if (ok == false)
    KT_W("cancel_action_goal('%s') returned false", actionP->name);

  //
  // Drop the local DdsActionGoal record (best-effort - the actual final
  // status arrives later via ddsActionStatusNotification).
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
      free(gP->requestJson);
      free(gP);
      break;
    }
    prev = gP;
  }

  return ok;
}
