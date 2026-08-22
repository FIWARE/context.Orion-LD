/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaStrdup.h"                                     // kaStrdup
}

#include "orionld/types/OrionldGeoIndex.h"                       // OrionldGeoIndex
#include "orionld/common/orionldState.h"                         // kalloc, geoIndexList
#include "orionld/common/kallocGuard.h"                          // kallocGuardedAlloc, kallocGuardedStrdup
#include "orionld/db/dbGeoIndexAdd.h"                            // Own interface



// ----------------------------------------------------------------------------
//
// dbGeoIndexAdd -
//
void dbGeoIndexAdd(const char* tenant, const char* attrName)
{
  OrionldGeoIndex* geoNodeP = (OrionldGeoIndex*) kallocGuardedAlloc(&kalloc, sizeof(OrionldGeoIndex));

  geoNodeP->tenant   = kallocGuardedStrdup(&kalloc, tenant);
  geoNodeP->attrName = kallocGuardedStrdup(&kalloc, attrName);

  if (geoIndexList == NULL)
  {
    geoNodeP->next = NULL;
    geoIndexList   = geoNodeP;
  }
  else
  {
    geoNodeP->next = geoIndexList;
    geoIndexList   = geoNodeP;
  }
}
