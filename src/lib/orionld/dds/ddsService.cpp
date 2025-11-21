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
#include <stdint.h>                                              // types: uint64_t, ...

#include "ddsenabler/DDSEnabler.hpp"                             // DDSEnabler::send_service_request

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/types/DdsService.h"                            // DdsService
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler



// -----------------------------------------------------------------------------
//
// ddsService
//
void ddsService(DdsService* serviceP, KjNode* attributeValueP)
{
  int   jsonLen = kjFastRenderSize(attributeValueP);
  char* json    = kaAlloc(&orionldState.kalloc, jsonLen + 20);

  kjFastRender(attributeValueP, json);

  KT_T(StDdsService, "Servicing '%s'", serviceP->name);

  //
  // Create the instance and add it to the 'instances' list
  //
  DdsServiceInstance* dsiP = (DdsServiceInstance*) malloc(sizeof(DdsServiceInstance));
  dsiP->requestId     = 0;
  dsiP->next          = serviceP->instances;
  serviceP->instances = dsiP;

  // Start the service
  ddsEnabler->send_service_request(serviceP->name, json, dsiP->requestId, eprosima::ddsenabler::participants::Protocol::ROS2);
  KT_T(StDdsService, "Started Service '%s' (req id: %llu)", serviceP->name, dsiP->requestId);
}
