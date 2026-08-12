#ifndef SRC_LIB_ORIONLD_REGCACHE_REGCACHEITEMFROMDB_H_
#define SRC_LIB_ORIONLD_REGCACHE_REGCACHEITEMFROMDB_H_

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
extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/RegCache.h"                              // RegCache



// -----------------------------------------------------------------------------
//
// regCacheItemFromDbTree - cache one registration, given its DATABASE-model tree
//
// The body of what regCacheCreate does to each registration it finds at startup.
// ⚠️ 'dbRegP' is converted IN PLACE - it comes back as the API model.
//
extern bool regCacheItemFromDbTree(RegCache* rcP, KjNode* dbRegP);



// -----------------------------------------------------------------------------
//
// regCacheItemFromDb - (re)build ONE cached registration from the database
//
// Twin of subCacheItemFromDb. Reads the registration back and puts it in the
// cache, replacing whatever was there - which is what an instance has to do when
// another instance creates or changes a registration.
//
extern bool regCacheItemFromDb(OrionldTenant* tenantP, const char* registrationId);

#endif  // SRC_LIB_ORIONLD_REGCACHE_REGCACHEITEMFROMDB_H_
