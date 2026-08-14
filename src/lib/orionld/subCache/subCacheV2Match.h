#ifndef SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEV2MATCH_H_
#define SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEV2MATCH_H_

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
#include <string>                                                // std::string
#include <vector>                                                // std::vector

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/SubCacheItem.h"                           // SubCacheItem



// -----------------------------------------------------------------------------
//
// subCacheV2Match - the NGSIv2 matching, over the new subscription cache
//
extern int subCacheV2Match
(
  OrionldTenant*                  tenantP,
  const char*                     servicePath,
  const char*                     entityId,
  const char*                     entityType,
  const std::vector<std::string>& attrV,
  std::vector<SubCacheItem*>*     subVecP
);

#endif  // SRC_LIB_ORIONLD_SUBCACHE_SUBCACHEV2MATCH_H_
