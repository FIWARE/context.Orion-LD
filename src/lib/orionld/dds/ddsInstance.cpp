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
#include <stdlib.h>                                              // free
#include <pthread.h>                                             // pthread_mutex_*

extern "C"
{
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
}

#include "orionld/types/DdsService.h"                            // DdsService, DdsServiceInstance
#include "orionld/dds/ddsInstance.h"                             // Own interface



// -----------------------------------------------------------------------------
//
// ddsInstancePush - prepend an instance to the service's list.
//
void ddsInstancePush(DdsService* serviceP, DdsServiceInstance* dsiP)
{
  pthread_mutex_lock(&serviceP->instancesMtx);
  dsiP->next          = serviceP->instances;
  serviceP->instances = dsiP;
  pthread_mutex_unlock(&serviceP->instancesMtx);
}



// -----------------------------------------------------------------------------
//
// ddsInstancePop - unlink the instance with matching requestId.
//
DdsServiceInstance* ddsInstancePop(DdsService* serviceP, uint64_t requestId)
{
  pthread_mutex_lock(&serviceP->instancesMtx);

  DdsServiceInstance*  prev = NULL;
  DdsServiceInstance*  cur  = serviceP->instances;

  while (cur != NULL)
  {
    if (cur->requestId == requestId)
    {
      if (prev == NULL)
        serviceP->instances = cur->next;
      else
        prev->next = cur->next;

      cur->next = NULL;
      pthread_mutex_unlock(&serviceP->instancesMtx);
      return cur;
    }
    prev = cur;
    cur  = cur->next;
  }

  pthread_mutex_unlock(&serviceP->instancesMtx);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsInstanceFree - tear down a popped DdsServiceInstance.
//
void ddsInstanceFree(DdsServiceInstance* dsiP)
{
  if (dsiP == NULL)
    return;
  pthread_mutex_destroy(&dsiP->mtx);
  pthread_cond_destroy(&dsiP->cv);
  kaBufferReset(&dsiP->kalloc, KFALSE);
  free(dsiP);
}
