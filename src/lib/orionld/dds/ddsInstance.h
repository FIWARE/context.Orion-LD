#ifndef SRC_LIB_ORIONLD_DDS_DDSINSTANCE_H_
#define SRC_LIB_ORIONLD_DDS_DDSINSTANCE_H_

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
#include <stdint.h>                                              // uint64_t

#include "orionld/types/DdsService.h"                            // DdsService, DdsServiceInstance



// -----------------------------------------------------------------------------
//
// Shared lifecycle for DdsServiceInstance. Used from both the request path
// (ddsService.cpp - ddsSync timeout, pops its own instance) and the reply
// path (ddsServiceReplyNotification.cpp). Access to serviceP->instances is
// serialized by serviceP->instancesMtx.
//

// Prepend a newly-created instance to the service's list.
extern void ddsInstancePush(DdsService* serviceP, DdsServiceInstance* dsiP);

// Pop the instance matching requestId from the service's list, or NULL if
// not found. Thread-safe.
extern DdsServiceInstance* ddsInstancePop(DdsService* serviceP, uint64_t requestId);

// Tear down a popped (unlinked) instance and release its memory.
extern void ddsInstanceFree(DdsServiceInstance* dsiP);

#endif  // SRC_LIB_ORIONLD_DDS_DDSINSTANCE_H_
