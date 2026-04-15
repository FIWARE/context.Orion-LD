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
#include <stdlib.h>                                              // malloc
#include <time.h>                                                // time

#include "ddsenabler/DDSEnabler.hpp"                             // DDSEnabler::send_service_request

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaBufferInit.h"                                 // kaBufferInit
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBufferCreate.h"                                // kjBufferCreate
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/types/DdsService.h"                            // DdsService, DdsServiceInstance
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler



// -----------------------------------------------------------------------------
//
// ddsService
//
bool ddsService(DdsService* serviceP, KjNode* attributeValueP)
{
  int   jsonLen = kjFastRenderSize(attributeValueP);
  char* json    = kaAlloc(&orionldState.kalloc, jsonLen + 20);

  kjFastRender(attributeValueP, json);

  KT_T(StDdsService, "Servicing '%s'", serviceP->name);

  //
  // Create the instance and add it to the 'instances' list.
  //
  // The instance owns its own KAlloc/Kjson buffer pair so the cloned request
  // KjNode tree survives from this (request) thread until the reply arrives on
  // the DDS callback thread - well past the lifetime of orionldState.kalloc.
  //
  DdsServiceInstance* dsiP = (DdsServiceInstance*) malloc(sizeof(DdsServiceInstance));
  if (dsiP == NULL)
  {
    KT_E("Out of memory allocating DdsServiceInstance for service '%s'", serviceP->name);
    return false;
  }

  dsiP->requestId   = 0;
  dsiP->requestTree = NULL;
  dsiP->publishedAt = (int64_t) time(NULL);
  dsiP->next        = serviceP->instances;

  kaBufferInit(&dsiP->kalloc, NULL, 0, 4096, NULL, "DdsServiceInstance KAlloc");
  if (kjBufferCreate(&dsiP->kjson, &dsiP->kalloc) == NULL)
  {
    KT_E("kjBufferCreate failed for DdsServiceInstance of service '%s'", serviceP->name);
    free(dsiP);
    return false;
  }

  dsiP->requestTree = kjClone(&dsiP->kjson, attributeValueP);

  serviceP->instances = dsiP;

  //
  // send_service_request fails immediately if the enabler has not yet
  // discovered a DDS server for this service. Surface that back to the HTTP
  // caller as 503 ServiceUnavailable - the NGSI-LD update has already been
  // written to the DB, but the DDS side of the bridge could not deliver.
  //
  if (!ddsEnabler->send_service_request(serviceP->name, json, dsiP->requestId, eprosima::ddsenabler::participants::Protocol::ROS2))
  {
    KT_E("send_service_request failed for service '%s' (no DDS server discovered yet?)", serviceP->name);
    orionldError(OrionldInternalError, "DDS service unavailable", serviceP->name, 503);
    return false;
  }

  KT_T(StDdsService, "Started Service '%s' (req id: %llu)", serviceP->name, dsiP->requestId);
  return true;
}
