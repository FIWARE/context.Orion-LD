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
#include "orionld/dds/ddsActionStatusNotification.h"             // Own interface



// -----------------------------------------------------------------------------
//
// statusCodeToString -
//
static const char* statusCodeToString(eprosima::ddsenabler::participants::StatusCode code)
{
  switch (code)
  {
  case eprosima::ddsenabler::participants::StatusCode::UNKNOWN:              return "UNKNOWN";
  case eprosima::ddsenabler::participants::StatusCode::ACCEPTED:             return "ACCEPTED";
  case eprosima::ddsenabler::participants::StatusCode::EXECUTING:            return "EXECUTING";
  case eprosima::ddsenabler::participants::StatusCode::CANCELING:            return "CANCELING";
  case eprosima::ddsenabler::participants::StatusCode::SUCCEEDED:            return "SUCCEEDED";
  case eprosima::ddsenabler::participants::StatusCode::CANCELED:             return "CANCELED";
  case eprosima::ddsenabler::participants::StatusCode::ABORTED:              return "ABORTED";
  case eprosima::ddsenabler::participants::StatusCode::REJECTED:             return "REJECTED";
  case eprosima::ddsenabler::participants::StatusCode::TIMEOUT:              return "TIMEOUT";
  case eprosima::ddsenabler::participants::StatusCode::FAILED:               return "FAILED";
  case eprosima::ddsenabler::participants::StatusCode::CANCEL_REQUEST_FAILED: return "CANCEL_REQUEST_FAILED";
  }
  return "UNKNOWN";
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

  //
  // Build a JSON object for the status: { "code": "EXECUTING", "message": "..." }
  //
  orionldStateInit(NULL);

  KjNode* statusTree = kjObject(orionldState.kjsonP, NULL);
  KjNode* codeNode   = kjString(orionldState.kjsonP, "code", statusCodeToString(statusCode));
  KjNode* msgNode    = kjString(orionldState.kjsonP, "message", (statusMessage != NULL) ? statusMessage : "");

  kjChildAdd(statusTree, codeNode);
  kjChildAdd(statusTree, msgNode);

  ddsActionSubAttributeUpdate(actionP->entityId, actionP->entityType, actionP->attributeName, "ddsActionStatus", statusTree, publishTime);
}
