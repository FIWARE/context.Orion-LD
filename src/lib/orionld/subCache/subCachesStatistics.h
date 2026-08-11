#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHESSTATISTICS_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHESSTATISTICS_H_

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
// subCachesItems - the number of subscriptions cached, over all tenants
//
extern int subCachesItems(void);



// -----------------------------------------------------------------------------
//
// subCachesStatisticsGet - what GET /cache/statistics answers
//
// 'list' is filled with the ids of the cached subscriptions, comma separated, and
// left empty if they do not all fit.
//
// There is no "refreshes" counter. It counted the reloads of the old poll-the-
// database sync, which the new cache does not do - it is maintained item by item
// as subscriptions are created, patched and deleted.
//
extern void subCachesStatisticsGet(int* inserts, int* removes, int* updates, int* items, char* list, int listSize);



// -----------------------------------------------------------------------------
//
// subCachesStatisticsReset - what DELETE /cache/statistics does
//
extern void subCachesStatisticsReset(const char* by);



// -----------------------------------------------------------------------------
//
// The counters themselves - stepped by subCacheItemAdd/Remove/Update
//
extern int subCachesInserts;
extern int subCachesRemoves;
extern int subCachesUpdates;

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHESSTATISTICS_H_
