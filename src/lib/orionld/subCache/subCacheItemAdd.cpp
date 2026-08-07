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
#include <stdlib.h>                                              // calloc, free
#include <string.h>                                              // strdup

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjChildAdd, kjInteger, kjFloat, kjString
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheItemCompile.h"                // subCacheItemCompile
#include "orionld/subCache/subCacheItemAdd.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// subCounterAdd - seed a counter in the subscription tree, if not already there
//
static void subCounterAdd(KjNode* subP, const char* name)
{
  if (kjLookup(subP, name) == NULL)
    kjChildAdd(subP, kjInteger(NULL, name, 0));
}



// -----------------------------------------------------------------------------
//
// subTimestampAdd - seed a timestamp in the subscription tree, if not already there
//
static void subTimestampAdd(KjNode* subP, const char* name)
{
  if (kjLookup(subP, name) == NULL)
    kjChildAdd(subP, kjFloat(NULL, name, 0));
}



// -----------------------------------------------------------------------------
//
// subStringAdd - seed a string in the subscription tree, if not already there
//
static void subStringAdd(KjNode* subP, const char* name, const char* value)
{
  if (kjLookup(subP, name) == NULL)
    kjChildAdd(subP, kjString(NULL, name, value));
}



// -----------------------------------------------------------------------------
//
// subCacheItemAdd -
//
SubCacheItem* subCacheItemAdd
(
  SubCache*        scP,
  const char*      subscriptionId,
  KjNode*          subP,
  bool             fromDb,
  OrionldContext*  jsonldContextP
)
{
  SubCacheItem* sciP = (SubCacheItem*) calloc(1, sizeof(SubCacheItem));

  if (sciP == NULL)
    KT_X(1, "Out of memory attempting to allocate a Subscription Cache Item (%d bytes)", sizeof(SubCacheItem));

  KT_T(KtSubCache, "Adding sub '%s' into the sub cache for tenant '%s'", subscriptionId, scP->tenantP->mongoDbName);

  //
  // Append - the cache keeps insertion order, exactly as the reg cache does.
  //
  if (scP->last == NULL)
    scP->subList = sciP;
  else
    scP->last->next = sciP;
  scP->last = sciP;

  sciP->subId    = strdup(subscriptionId);
  sciP->subTree  = kjClone(NULL, subP);
  sciP->contextP = jsonldContextP;
  sciP->dirty    = false;
  sciP->next     = NULL;

  //
  // The deltas are what keeps mongo out of the notification path: every
  // notification bumps them in RAM and they are flushed (added, not written)
  // now and then. They always start at zero for a freshly cached item - what
  // is already in the database is the database's business.
  //
  sciP->deltas.timesSent   = 0;
  sciP->deltas.timesFailed = 0;
  sciP->deltas.lastSuccess = 0;
  sciP->deltas.lastFailure = 0;

  KjNode* hostAliasP = kjLookup(sciP->subTree, "hostAlias");
  if (hostAliasP != NULL)
    sciP->hostAlias = strdup(hostAliasP->value.s);

  //
  // A subscription that arrives from an API request has none of the bookkeeping
  // members yet - one coming from the database has them already.
  //
  if (fromDb == false)
  {
    subCounterAdd(sciP->subTree,   "timesSent");
    subCounterAdd(sciP->subTree,   "timesFailed");
    subTimestampAdd(sciP->subTree, "lastSuccess");
    subTimestampAdd(sciP->subTree, "lastFailure");
    subStringAdd(sciP->subTree,    "status", "active");
  }

  KjNode* modifiedAtP = kjLookup(sciP->subTree, "modifiedAt");
  if (modifiedAtP != NULL)
    sciP->modifiedAt = (modifiedAtP->type == KjFloat)? modifiedAtP->value.f : modifiedAtP->value.i;

  //
  // The compiled matching state comes LAST, and it is built from sciP->subTree -
  // the item's own clone. The caller's tree is request-scoped, so regexes, QNode
  // trees and GEOS geometries built from it would dangle after the request.
  //
  subCacheItemCompile(sciP);

  return sciP;
}
