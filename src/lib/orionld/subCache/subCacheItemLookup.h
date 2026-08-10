#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMLOOKUP_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMLOOKUP_H_

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
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem



// -----------------------------------------------------------------------------
//
// subCacheItemLookup - find a subscription in a tenant's subscription cache
//
// Twin of regCacheItemLookup, except that the id is right there in the item
// (SubCacheItem::subId), so no kjLookup per item is needed.
//
extern SubCacheItem* subCacheItemLookup(SubCache* scP, const char* subId);



// -----------------------------------------------------------------------------
//
// subCacheItemLookupByTenantName - the same, for callers that have the tenant NAME
//
// The NGSIv2 notification machinery carries the tenant as a string - that is what
// SenderThreadParams holds - while the sub cache hangs off the OrionldTenant.
//
extern SubCacheItem* subCacheItemLookupByTenantName(const char* tenantName, const char* subId);

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEITEMLOOKUP_H_
