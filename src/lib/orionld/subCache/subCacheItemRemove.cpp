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
#include <unistd.h>                                              // NULL
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheItemRelease.h"                // subCacheItemRelease
#include "orionld/subCache/subCacheItemRemove.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemRemove -
//
// Looking an individual subscription up for deletion is FAR from what the cache is
// for - it is for matching entity alterations fast. So, a linear search is fine here.
//
bool subCacheItemRemove(SubCache* scP, const char* subId)
{
  if (scP == NULL)
    KT_RE(false, "NULL scP - that's a SW bug!");

  SubCacheItem* sciP = scP->subList;
  SubCacheItem* prev = NULL;

  while (sciP != NULL)
  {
    if ((sciP->subId != NULL) && (strcmp(sciP->subId, subId) == 0))
    {
      KT_T(KtSubCache, "Removing sub '%s' from the sub cache for tenant '%s'", subId, scP->tenantP->mongoDbName);

      if (prev == NULL)              // It's the first item - step over it
        scP->subList = sciP->next;
      else
        prev->next = sciP->next;

      if (scP->last == sciP)         // It was the last item - 'prev' is the new last
        scP->last = prev;

      subCacheItemRelease(sciP);
      return true;
    }

    prev = sciP;
    sciP = sciP->next;
  }

  KT_T(KtSubCache, "Sub '%s' not found in the sub cache for tenant '%s'", subId, scP->tenantP->mongoDbName);
  return false;
}
