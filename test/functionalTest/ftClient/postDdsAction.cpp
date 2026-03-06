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
#include <memory>                                            // std::shared_ptr
#include <string>                                            // std::string

#include "ddsenabler/dds_enabler_runner.hpp"                 // DDSEnabler

extern "C"
{
#include "ktrace/kTrace.h"                                   // trace messages - ktrace library
#include "kjson/KjNode.h"                                    // KjNode
#include "kjson/kjLookup.h"                                  // kjLookup
#include "kjson/kjBuilder.h"                                 // kjObject, kjString, kjBoolean
}

#include "common/orionldState.h"                             // orionldState
#include "common/traceLevels.h"                              // Trace levels for ktrace

#include "ftClient/ftErrorResponse.h"                        // ftErrorResponse
#include "ftClient/postDdsAction.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// ddsEnabler - defined in ftClient.cpp
//
extern std::shared_ptr<eprosima::ddsenabler::DDSEnabler> ddsEnabler;
extern bool ddsSupport;



// -----------------------------------------------------------------------------
//
// postDdsAction -
//
// POST /dds/action
// {
//   "name": "fibonacci"
// }
//
// Announces ftClient as a DDS action server for the given action name.
// Type definitions must be loaded first via POST /dds/type.
//
KjNode* postDdsAction(int* statusCodeP)
{
  if (ddsSupport == false)
  {
    *statusCodeP = 501;
    return ftErrorResponse(501, "DDS Support not enabled", "restart with --dds");
  }

  if (ddsEnabler == nullptr)
  {
    *statusCodeP = 500;
    return ftErrorResponse(500, "Internal Error", "DDS Enabler not initialized");
  }

  if (orionldState.requestTree == NULL)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Bad Request", "No payload body");
  }

  KjNode* nameNode = kjLookup(orionldState.requestTree, "name");
  if (nameNode == NULL || nameNode->type != KjString)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Bad Request", "Missing or invalid 'name' field");
  }

  const char* actionName = nameNode->value.s;

  KT_V("Announcing DDS action '%s'", actionName);

  bool result = ddsEnabler->announce_action(actionName);

  KjNode* response    = kjObject(NULL, NULL);
  KjNode* nameResp    = kjString(NULL, "action", actionName);
  KjNode* successResp = kjBoolean(NULL, "announced", result);

  kjChildAdd(response, nameResp);
  kjChildAdd(response, successResp);

  if (result)
  {
    KT_V("Successfully announced DDS action '%s'", actionName);
    *statusCodeP = 201;
  }
  else
  {
    KT_E("Failed to announce DDS action '%s'", actionName);
    *statusCodeP = 500;
  }

  return response;
}
