#ifndef SRC_LIB_ORIONLD_HA_HAINIT_H_
#define SRC_LIB_ORIONLD_HA_HAINIT_H_

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



// -----------------------------------------------------------------------------
//
// haInit - start the HA channel named by -ha
//
// Called after the mongo connection is up (which channels are possible depends
// on it) and BEFORE the caches are loaded from the database. That order is what
// closes the startup gap: a change made by another instance between "we read the
// database" and "we started listening" would otherwise be missed for the
// lifetime of the process. Listening first means the two overlap instead, and an
// event for something the load also brings in is applied twice - which costs a
// re-read and changes nothing.
//
extern bool haInit(void);



// -----------------------------------------------------------------------------
//
// haApplyEnable - the caches are loaded; events may now be applied
//
extern void haApplyEnable(void);



// -----------------------------------------------------------------------------
//
// haApplyWait - block until the caches are loaded
//
// ⚠️ EVERY CHANNEL CALLS THIS before it does anything with an event - before it
// even resolves which tenant the event belongs to, because resolving one CREATES
// it, and a tenant invented while the startup load is walking the tenant list
// would get its caches filled with that one item and nothing else.
//
// It blocks only during startup, and only if an event arrives that early.
//
extern void haApplyWait(void);

#endif  // SRC_LIB_ORIONLD_HA_HAINIT_H_
