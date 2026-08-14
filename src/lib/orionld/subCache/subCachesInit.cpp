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
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant, tenant0
#include "orionld/common/tenantList.h"                           // tenantList
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheCreate.h"                     // subCacheCreate
#include "orionld/subCache/subCachesInit.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// subCachesInit -
//
void subCachesInit(void)
{
  KT_T(KtSubCache, "Creating subCache for default tenant");
  tenant0.subCache = subCacheCreate(&tenant0, true);


  for (OrionldTenant* tenantP = tenantList; tenantP != NULL; tenantP = tenantP->next)
  {
    KT_T(KtSubCache, "Creating subCache for tenant '%s'", tenantP->tenant);
    tenantP->subCache = subCacheCreate(tenantP, true);
  }
}
