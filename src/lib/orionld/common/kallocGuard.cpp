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
* Author: Carsten Frey
*/
#include <semaphore.h>                                         // sem_init, sem_wait, sem_post
#include <errno.h>                                             // errno
#include <string.h>                                            // strerror

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kalloc/kaAlloc.h"                                    // kaAlloc
#include "kalloc/kaStrdup.h"                                   // kaStrdup
}

#include "orionld/common/orionldState.h"                       // kalloc (the global arena)
#include "orionld/common/kallocGuard.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// kallocGuardSem - protects the process-global 'kalloc' arena
//
static sem_t kallocGuardSem;



// -----------------------------------------------------------------------------
//
// kallocGuardInit -
//
void kallocGuardInit(void)
{
  if (sem_init(&kallocGuardSem, 0, 1) == -1)
    KT_X(1, "Runtime Error (error initializing semaphore for the global kalloc arena; %s)", strerror(errno));
}



// -----------------------------------------------------------------------------
//
// kallocGuardTake - take the guard, but only for the global arena
//
// Used to protect allocations that happen inside a library (khashTableCreate/khashItemAdd allocate
// from the KAlloc they were given) and can't be routed via kallocGuardedAlloc().
//
void kallocGuardTake(KAlloc* kaP)
{
  if (kaP == &kalloc)
    sem_wait(&kallocGuardSem);
}



// -----------------------------------------------------------------------------
//
// kallocGuardGive -
//
void kallocGuardGive(KAlloc* kaP)
{
  if (kaP == &kalloc)
    sem_post(&kallocGuardSem);
}



// -----------------------------------------------------------------------------
//
// kallocGuardedAlloc -
//
char* kallocGuardedAlloc(KAlloc* kaP, unsigned long long size)
{
  if (kaP != &kalloc)   // Thread-local arena - uncontended
    return kaAlloc(kaP, size);

  sem_wait(&kallocGuardSem);
  char* buf = kaAlloc(kaP, size);
  sem_post(&kallocGuardSem);

  return buf;
}



// -----------------------------------------------------------------------------
//
// kallocGuardedStrdup -
//
char* kallocGuardedStrdup(KAlloc* kaP, const char* s)
{
  if (kaP != &kalloc)   // Thread-local arena - uncontended
    return kaStrdup(kaP, s);

  sem_wait(&kallocGuardSem);
  char* buf = kaStrdup(kaP, s);
  sem_post(&kallocGuardSem);

  return buf;
}
