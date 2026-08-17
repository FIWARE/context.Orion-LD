#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHESREFRESH_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHESREFRESH_H_

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
// subCachesRefresh - bring every tenant's subscription cache in line with the database
//
// This is the OLD way for two broker instances to learn of each other's
// subscriptions: poll the database. It is on its way out - mongo's change streams
// replace it - and it is off unless -subCacheIval says otherwise.
//
// Unlike the mechanism it replaces, nothing is wiped: each cached subscription is
// updated in place and only those no longer in the database are removed, so the
// notification counters that have not yet been flushed survive the refresh.
//
extern void subCachesRefresh(void);



// -----------------------------------------------------------------------------
//
// subCachesMaintenanceStart - start the sub cache maintenance thread
//
// Two independent deadlines:
//   -subCacheFlushIval  the notification counters are pushed to the database
//   -subCacheIval       the database is polled for what other instances have done
//
// The thread is not started at all if both are zero.
//
extern void subCachesMaintenanceStart(void);

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHESREFRESH_H_
