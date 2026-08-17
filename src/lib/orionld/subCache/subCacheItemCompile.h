#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMCOMPILE_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMCOMPILE_H_

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
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem



// -----------------------------------------------------------------------------
//
// subCacheItemCompile - build the matching state of a cached Subscription
//
// Everything that matching an entity alteration would otherwise have to parse -
// the 'q' filter, the geo query, the entity id patterns, the notification
// endpoint - is built here, ONCE, when the subscription enters the cache.
//
// IMPORTANT
//   This works on sciP->subTree, which is the cache item's OWN clone of the
//   subscription. It must be so: the tree that subCacheItemAdd is handed is
//   request-scoped (kalloc), and regexes/QNodes/GEOS geometries built from it
//   would point at memory that is reset once the request is over.
//
extern void subCacheItemCompile(SubCacheItem* sciP);

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMCOMPILE_H_
