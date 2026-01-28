/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include <map>                                               // std::map
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
#include "ftClient/postDdsService.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// ddsEnabler - defined in ftClient.cpp
//
extern std::shared_ptr<eprosima::ddsenabler::DDSEnabler> ddsEnabler;
extern bool ddsSupport;



// -----------------------------------------------------------------------------
//
// ddsServiceInfoMap - map of service info (serviceName -> DdsServiceInfo)
//
static std::map<std::string, DdsServiceInfo> ddsServiceInfoMap;



// -----------------------------------------------------------------------------
//
// ddsServiceInfoLookup - lookup service info by name
//
DdsServiceInfo* ddsServiceInfoLookup(const char* serviceName)
{
  auto it = ddsServiceInfoMap.find(serviceName);
  if (it != ddsServiceInfoMap.end())
    return &it->second;
  return NULL;
}



// -----------------------------------------------------------------------------
//
// postDdsService -
//
// POST /dds/service
// {
//   "name": "add_two_ints",
//   "requestType": "example_interfaces::srv::dds_::AddTwoInts_Request_",
//   "replyType": "example_interfaces::srv::dds_::AddTwoInts_Response_"
// }
//
// Announces ftClient as a DDS service server for the given service name.
// Type definitions must be loaded first via POST /dds/type.
//
KjNode* postDdsService(int* statusCodeP)
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

  const char* serviceName = nameNode->value.s;

  // Optional type fields
  KjNode* requestTypeNode = kjLookup(orionldState.requestTree, "requestType");
  KjNode* replyTypeNode = kjLookup(orionldState.requestTree, "replyType");

  // Store service info
  DdsServiceInfo serviceInfo;

  if (requestTypeNode != NULL && requestTypeNode->type == KjString)
    serviceInfo.requestType = requestTypeNode->value.s;
  else
    serviceInfo.requestType = std::string(serviceName) + "_Request";

  if (replyTypeNode != NULL && replyTypeNode->type == KjString)
    serviceInfo.replyType = replyTypeNode->value.s;
  else
    serviceInfo.replyType = std::string(serviceName) + "_Response";

  // Default QoS for ROS2 services
  serviceInfo.requestQos = "reliability: true\ndurability: false\nownership: false\nkeyed: false";
  serviceInfo.replyQos = "reliability: true\ndurability: false\nownership: false\nkeyed: false";

  ddsServiceInfoMap[serviceName] = serviceInfo;

  KT_V("Stored service info for '%s': reqType='%s', replyType='%s'",
       serviceName, serviceInfo.requestType.c_str(), serviceInfo.replyType.c_str());

  KT_V("Announcing DDS service '%s'", serviceName);

  bool result = ddsEnabler->announce_service(serviceName);

  KjNode* response = kjObject(NULL, NULL);
  KjNode* nameResp = kjString(NULL, "service", serviceName);
  KjNode* successResp = kjBoolean(NULL, "announced", result);
  KjNode* reqTypeResp = kjString(NULL, "requestType", serviceInfo.requestType.c_str());
  KjNode* repTypeResp = kjString(NULL, "replyType", serviceInfo.replyType.c_str());

  kjChildAdd(response, nameResp);
  kjChildAdd(response, successResp);
  kjChildAdd(response, reqTypeResp);
  kjChildAdd(response, repTypeResp);

  if (result)
  {
    KT_V("Successfully announced DDS service '%s'", serviceName);
    *statusCodeP = 201;
  }
  else
  {
    KT_E("Failed to announce DDS service '%s'", serviceName);
    *statusCodeP = 500;
  }

  return response;
}
