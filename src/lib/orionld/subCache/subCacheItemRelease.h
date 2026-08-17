#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMRELEASE_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMRELEASE_H_

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
// subCacheItemCompiledStateRelease - free everything subCacheItemCompile built
//
// The item itself survives, with its compiled members back at zero - that is what
// an update needs, to recompile from a new subTree.
//
extern void subCacheItemCompiledStateRelease(SubCacheItem* sciP);



// -----------------------------------------------------------------------------
//
// subCacheItemRelease - free a cache item and everything it owns
//
// The caller has already unlinked it from its SubCache.
//
extern void subCacheItemRelease(SubCacheItem* sciP);

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMRELEASE_H_
