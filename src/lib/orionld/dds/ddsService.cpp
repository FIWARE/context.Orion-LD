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
#include <stdint.h>                                              // uint64_t, int64_t
#include <stdlib.h>                                              // malloc
#include <string.h>                                              // strdup
#include <time.h>                                                // time, clock_gettime
#include <errno.h>                                               // ETIMEDOUT
#include <pthread.h>                                             // pthread_*

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
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler, ddsSyncTimeoutMs
#include "orionld/dds/ddsInstance.h"                             // ddsInstancePush, ddsInstancePop, ddsInstanceFree
#include "orionld/dds/ddsService.h"                              // Own interface



// -----------------------------------------------------------------------------
//
// deadlineFromNowMs - compute an absolute CLOCK_REALTIME deadline 'ms' ahead.
//
static void deadlineFromNowMs(struct timespec* deadline, int64_t ms)
{
  clock_gettime(CLOCK_REALTIME, deadline);
  deadline->tv_sec  += ms / 1000;
  deadline->tv_nsec += (ms % 1000) * 1000000L;
  if (deadline->tv_nsec >= 1000000000L)
  {
    deadline->tv_sec  += 1;
    deadline->tv_nsec -= 1000000000L;
  }
}



// -----------------------------------------------------------------------------
//
// ddsService
//
// Send a DDS service request triggered by an NGSI-LD attribute update.
//
// sync == false: fire and forget. Reply (if any) is handled by
// ddsServiceReplyNotification which performs the merge-patch itself.
//
// sync == true: block the calling thread until either the reply arrives or
// ddsSyncTimeoutMs elapses. On success '*dsiOut' receives the unlinked
// DdsServiceInstance (caller owns it and must call ddsInstanceFree after
// consuming replyTree/ddsDataType/etc.). On failure returns false and
// orionldError has been set (503 send-failure, 504 timeout) and any
// allocated instance has been freed.
//
bool ddsService(DdsService* serviceP, KjNode* attributeValueP, bool sync, DdsServiceInstance** dsiOut)
{
  if (dsiOut != NULL)
    *dsiOut = NULL;

  int   jsonLen = kjFastRenderSize(attributeValueP);
  char* json    = kaAlloc(&orionldState.kalloc, jsonLen + 20);

  kjFastRender(attributeValueP, json);

  KT_T(StDdsService, "Servicing '%s' (sync=%s)", serviceP->name, sync? "true" : "false");

  //
  // Create the instance. Owns its own KAlloc/Kjson buffer pair so the cloned
  // request (and in sync mode, later the reply) KjNode tree survives across
  // threads - the request thread's orionldState.kalloc dies at end-of-request.
  //
  DdsServiceInstance* dsiP = (DdsServiceInstance*) malloc(sizeof(DdsServiceInstance));
  if (dsiP == NULL)
  {
    KT_E("Out of memory allocating DdsServiceInstance for service '%s'", serviceP->name);
    orionldError(OrionldInternalError, "Out of memory", "DdsServiceInstance", 500);
    return false;
  }

  dsiP->requestId        = 0;
  dsiP->requestTree      = NULL;
  dsiP->publishedAt      = (int64_t) time(NULL);
  dsiP->syncMode         = sync;
  dsiP->replyReceived    = false;
  dsiP->replyTree        = NULL;
  dsiP->replyPublishedAt = 0;
  dsiP->ddsDataType      = NULL;
  dsiP->participantId    = NULL;
  dsiP->xId              = NULL;
  dsiP->next             = NULL;

  pthread_mutex_init(&dsiP->mtx, NULL);
  pthread_cond_init(&dsiP->cv, NULL);

  kaBufferInit(&dsiP->kalloc, NULL, 0, 4096, NULL, "DdsServiceInstance KAlloc");
  if (kjBufferCreate(&dsiP->kjson, &dsiP->kalloc) == NULL)
  {
    KT_E("kjBufferCreate failed for DdsServiceInstance of service '%s'", serviceP->name);
    ddsInstanceFree(dsiP);
    orionldError(OrionldInternalError, "kjBufferCreate failed", serviceP->name, 500);
    return false;
  }

  dsiP->requestTree = kjClone(&dsiP->kjson, attributeValueP);

  // Link into the service's list BEFORE send_service_request - the reply may
  // arrive on another thread before this one returns from the call.
  ddsInstancePush(serviceP, dsiP);

  if (!ddsEnabler->send_service_request(serviceP->name, json, dsiP->requestId,
                                        eprosima::ddsenabler::participants::Protocol::ROS2))
  {
    KT_E("send_service_request failed for service '%s' (no DDS server discovered yet?)", serviceP->name);
    // The reply will never come: unlink + free ourselves.
    DdsServiceInstance* popped = ddsInstancePop(serviceP, dsiP->requestId);
    ddsInstanceFree(popped);
    orionldError(OrionldInternalError, "DDS service unavailable", serviceP->name, 503);
    return false;
  }

  KT_T(StDdsService, "Started Service '%s' (req id: %llu)", serviceP->name, dsiP->requestId);

  if (!sync)
    return true;  // Fire-and-forget; reply thread owns the instance from here.

  //
  // Sync mode: block until the reply thread signals 'replyReceived' or the
  // deadline expires.
  //
  struct timespec deadline;
  deadlineFromNowMs(&deadline, ddsSyncTimeoutMs);

  pthread_mutex_lock(&dsiP->mtx);
  int rc = 0;
  while ((rc == 0) && !dsiP->replyReceived)
    rc = pthread_cond_timedwait(&dsiP->cv, &dsiP->mtx, &deadline);
  bool gotReply = dsiP->replyReceived;
  pthread_mutex_unlock(&dsiP->mtx);

  if (gotReply)
  {
    // Reply thread has already popped the instance from the list and filled
    // replyTree / ddsDataType / participantId / xId / replyPublishedAt. Hand
    // ownership to the caller; it will call ddsInstanceFree when done.
    if (dsiOut != NULL)
      *dsiOut = dsiP;
    else
      ddsInstanceFree(dsiP);
    return true;
  }

  // Timeout. Unlink the instance (if the reply thread didn't, in a very
  // late race, already pop it - in which case ddsInstancePop returns NULL
  // and we're safe). Log W: and surface 504.
  KT_W("DDS sync timeout after %lld ms for service '%s' (req id %llu)",
       (long long) ddsSyncTimeoutMs, serviceP->name, dsiP->requestId);
  DdsServiceInstance* popped = ddsInstancePop(serviceP, dsiP->requestId);
  if (popped == NULL)
  {
    // Reply thread popped between our timeout and our pop attempt. Check
    // replyReceived one more time - if true, the reply thread has filled
    // the instance and we still own it via dsiP.
    pthread_mutex_lock(&dsiP->mtx);
    bool lateReply = dsiP->replyReceived;
    pthread_mutex_unlock(&dsiP->mtx);

    if (lateReply)
    {
      KT_T(StDdsService, "Late reply raced timeout for service '%s' - using it", serviceP->name);
      if (dsiOut != NULL)
        *dsiOut = dsiP;
      else
        ddsInstanceFree(dsiP);
      return true;
    }

    // Still no reply flagged - unreachable without a bug. Avoid a leak:
    // the reply thread popped but somehow didn't set replyReceived. Free.
    ddsInstanceFree(dsiP);
  }
  else
  {
    ddsInstanceFree(popped);
  }

  orionldError(OrionldInternalError, "DDS service timeout", serviceP->name, 504);
  return false;
}
