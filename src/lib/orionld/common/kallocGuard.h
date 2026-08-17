#ifndef SRC_LIB_ORIONLD_COMMON_KALLOCGUARD_H_
#define SRC_LIB_ORIONLD_COMMON_KALLOCGUARD_H_

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
extern "C"
{
#include "kalloc/KAlloc.h"                                     // KAlloc
}



// -----------------------------------------------------------------------------
//
// kallocGuard - serialize allocations from the process-global 'kalloc' arena
//
// A KAlloc arena is a plain bump allocator with NO locking of its own - the sem_wait/sem_post calls
// inside kaAlloc() are commented out in the kalloc library. That is harmless for the per-request
// arenas (orionldState.kalloc), which are thread-local, but NOT for the process-global 'kalloc',
// which is written to by every HTTP thread and by every Kafka consumer thread.
//
// Two concurrent kaAlloc() calls on the same arena race on allocPointer/bytesLeft/allocList: both
// threads can be handed the SAME chunk, and 'bytesLeft -= size' applied to a stale value underflows
// (bytesLeft is unsigned), after which the allocator hands out pointers far past the end of its
// buffer - straight into other heap chunks. The result is a SIGSEGV or one of glibc's
// "malloc(): invalid size" / "corrupted size vs prev_size" / "free(): invalid pointer" aborts.
//
// Every allocation from the global 'kalloc' must therefore go through this module. Allocations from
// a thread-local arena are passed straight through, without taking the semaphore.
//
extern void   kallocGuardInit(void);
extern void   kallocGuardTake(KAlloc* kaP);
extern void   kallocGuardGive(KAlloc* kaP);
extern char*  kallocGuardedAlloc(KAlloc* kaP, unsigned long long size);
extern char*  kallocGuardedStrdup(KAlloc* kaP, const char* s);

#endif  // SRC_LIB_ORIONLD_COMMON_KALLOCGUARD_H_
