#ifndef SRC_LIB_ORIONLD_TROE_PGSEM_H_
#define SRC_LIB_ORIONLD_TROE_PGSEM_H_

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
#include <semaphore.h>                                         // sem_t



// -----------------------------------------------------------------------------
//
// pgSemWait - sem_wait, restarted on signal interruption (EINTR)
//
extern void pgSemWait(sem_t* semP);



// -----------------------------------------------------------------------------
//
// pgSemTimedWait - sem_timedwait with a relative timeout, restarted on EINTR
//
// RETURN VALUE
//   true   the semaphore was acquired
//   false  the timeout expired (or sem_timedwait failed)
//
extern bool pgSemTimedWait(sem_t* semP, int timeoutSecs);

#endif  // SRC_LIB_ORIONLD_TROE_PGSEM_H_
