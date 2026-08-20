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
#include <semaphore.h>                                         // sem_wait, sem_timedwait
#include <errno.h>                                             // errno, EINTR, ETIMEDOUT
#include <time.h>                                              // clock_gettime, timespec

#include "orionld/troe/pgSem.h"                                // Own interface



// -----------------------------------------------------------------------------
//
// pgSemWait -
//
// A sem_wait interrupted by a signal returns -1/EINTR without the semaphore taken.
// Proceeding anyway would enter the critical section without mutual exclusion,
// so the wait must be restarted.
//
void pgSemWait(sem_t* semP)
{
  while ((sem_wait(semP) == -1) && (errno == EINTR))
  {
  }
}



// -----------------------------------------------------------------------------
//
// pgSemTimedWait -
//
bool pgSemTimedWait(sem_t* semP, int timeoutSecs)
{
  struct timespec deadline;

  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)  // sem_timedwait is specified over CLOCK_REALTIME
  {
    pgSemWait(semP);
    return true;
  }

  deadline.tv_sec += timeoutSecs;

  while (sem_timedwait(semP, &deadline) == -1)
  {
    if (errno == EINTR)
      continue;

    return false;  // ETIMEDOUT (or an unexpected error - treated as a timeout)
  }

  return true;
}
