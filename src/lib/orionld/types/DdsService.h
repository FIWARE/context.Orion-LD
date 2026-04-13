#ifndef SRC_LIB_ORIONLD_TYPES_DDSSERVICE_H_
#define SRC_LIB_ORIONLD_TYPES_DDSSERVICE_H_

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

extern "C"
{
#include "kalloc/KAlloc.h"                                       // KAlloc
#include "kjson/kjson.h"                                         // Kjson
#include "kjson/KjNode.h"                                        // KjNode
}



// -----------------------------------------------------------------------------
//
// DdsServiceInstance -
//
// One-per-in-flight-request. Owns its own KAlloc/Kjson buffer pair so the cloned
// request KjNode tree survives from the request thread until the reply arrives
// on the DDS callback thread. Released by kaBufferReset(&kalloc, false) + free().
//
typedef struct DdsServiceInstance
{
  uint64_t                    requestId;
  KAlloc                      kalloc;        // dedicated allocator for the request tree
  Kjson                       kjson;         // kjson context built on 'kalloc'
  KjNode*                     requestTree;   // cloned request payload (lives in 'kalloc')
  int64_t                     publishedAt;   // wall-clock seconds since epoch when the request was sent
  struct DdsServiceInstance*  next;
} DdsServiceInstance;



// -----------------------------------------------------------------------------
//
// DdsService -
//
typedef struct DdsService
{
  char*                name;
  char*                entityId;
  char*                entityType;
  char*                attributeName;
  char*                requestType;
  char*                requestQoS;
  char*                replyType;
  char*                replyQoS;
  uint64_t             requestId;
  DdsServiceInstance*  instances;
  struct DdsService*   next;
} DdsService;

#endif  // SRC_LIB_ORIONLD_TYPES_DDSSERVICE_H_
