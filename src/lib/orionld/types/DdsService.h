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
#include <pthread.h>                                             // pthread_mutex_t, pthread_cond_t

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
// Sync-mode fields are only used when the triggering PATCH asked for
// synchronous DDS semantics (see ddsSync opt-in). In sync mode, the request
// thread blocks on 'cv' until the reply thread signals 'replyReceived' or
// the deadline expires. The reply thread parses the payload into the instance
// and signals; the request thread then owns the merge into the entity tree.
//
typedef struct DdsServiceInstance
{
  uint64_t                    requestId;
  KAlloc                      kalloc;          // dedicated allocator for the request (and reply) tree
  Kjson                       kjson;           // kjson context built on 'kalloc'
  KjNode*                     requestTree;     // cloned request payload (lives in 'kalloc')
  int64_t                     publishedAt;     // wall-clock seconds since epoch when the request was sent

  // Sync-mode (ddsSync=true) fields - unused and zero-initialized in async mode.
  bool                        syncMode;        // true if the request thread is waiting for the reply
  pthread_mutex_t             mtx;             // guards replyReceived / replyTree / replyMeta fields below
  pthread_cond_t              cv;              // signalled by the reply thread when replyReceived becomes true
  bool                        replyReceived;   // set true by reply thread just before pthread_cond_signal
  KjNode*                     replyTree;       // parsed reply value (lives in 'kalloc')
  int64_t                     replyPublishedAt;// reply's publishedAt in seconds (after ns->s conversion)
  char*                       ddsDataType;     // reply envelope: "rr/<service>Reply.type" (strdup'd in 'kalloc')
  char*                       participantId;   // reply envelope: top-level "id" (strdup'd in 'kalloc')
  char*                       xId;             // reply envelope: key under rr/<service>Reply.data (strdup'd in 'kalloc')

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
  pthread_mutex_t      instancesMtx;   // guards 'instances' list: push (request thread) + pop (reply or timeout)
  struct DdsService*   next;
} DdsService;

#endif  // SRC_LIB_ORIONLD_TYPES_DDSSERVICE_H_
