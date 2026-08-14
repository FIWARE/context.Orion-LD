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
#include <stdlib.h>                                              // free
#include <string.h>                                              // strdup

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjFree.h"                                        // kjFree
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/apiModelToCacheSubscription.h"        // apiModelToCacheSubscription
#include "orionld/subCache/subCacheItemCompile.h"                // subCacheItemCompile
#include "orionld/subCache/subCacheItemRelease.h"                // subCacheItemCompiledStateRelease
#include "orionld/subCache/subCachesStatistics.h"          // subCachesUpdates
#include "orionld/subCache/subCacheItemUpdate.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemUpdate -
//
void subCacheItemUpdate(SubCacheItem* sciP, KjNode* subP, OrionldContext* jsonldContextP)
{
  KT_T(KtSubCache, "Updating sub '%s' in the sub cache", sciP->subId);

  //
  // The deltas are NOT reset. They count notifications that really went out and that
  // have not yet been flushed to the database - a PATCH of the subscription doesn't
  // make them un-happen.
  //
  subCacheItemCompiledStateRelease(sciP);

  kjFree(sciP->subTree);
  sciP->subTree = kjClone(NULL, subP);

  if (jsonldContextP != NULL)
    sciP->contextP = jsonldContextP;

  apiModelToCacheSubscription(sciP->subTree, sciP->contextP);

  if (sciP->hostAlias != NULL)
  {
    free(sciP->hostAlias);
    sciP->hostAlias = NULL;
  }

  KjNode* hostAliasP = kjLookup(sciP->subTree, "hostAlias");
  if (hostAliasP != NULL)
    sciP->hostAlias = strdup(hostAliasP->value.s);

  KjNode* modifiedAtP = kjLookup(sciP->subTree, "modifiedAt");
  if (modifiedAtP != NULL)
    sciP->modifiedAt = (modifiedAtP->type == KjFloat)? modifiedAtP->value.f : modifiedAtP->value.i;

  subCacheItemCompile(sciP);

  ++subCachesUpdates;
}
