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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocContextCacheGetById.h"            // mongocContextCacheGetById
#include "orionld/contextCache/orionldContextCacheLookup.h"      // orionldContextCacheLookup
#include "orionld/contextCache/orionldContextCacheInit.h"        // dbContextToCache - the per-context half of the startup load
#include "orionld/contextCache/orionldContextCacheItemFromDb.h"  // Own interface



// -----------------------------------------------------------------------------
//
// orionldContextCacheItemFromDb -
//
// Cache ONE @context, read back from the database. Twin of subCacheItemFromDb and
// regCacheItemFromDb, and like them it goes through the very function the startup
// load uses (dbContextToCache), so a context that arrived over HA is cached exactly
// as one read at boot.
//
// The 'keyValues' argument is what the startup load decides by looking at the type
// of "value": an Object is a key-value context, an Array is a list of other
// contexts. Startup does the Objects first and the Arrays after, precisely because
// an Array refers to contexts that have to exist first. One arriving alone here has
// the same requirement - but the instance that created it persisted its members
// first, and a change stream delivers in the order the writes happened, so by the
// time this one shows up its members have already come past.
//
bool orionldContextCacheItemFromDb(const char* contextId)
{
  KjNode* dbContextP = mongocContextCacheGetById(contextId);

  if (dbContextP == NULL)
    KT_RE(false, "@context '%s' not found in the database - not cached", contextId);

  KjNode* valueNodeP = kjLookup(dbContextP, "value");

  if (valueNodeP == NULL)
    KT_RE(false, "@context '%s' has no 'value' in the database - not cached", contextId);

  if ((valueNodeP->type != KjObject) && (valueNodeP->type != KjArray))
    KT_RE(false, "@context '%s' is neither an object nor an array - not cached", contextId);

  //
  // Already cached? Then there is nothing to do. A @context does not change: it is
  // written once, with a URL (or a generated id) that identifies its content, and
  // the only reason to see an event for one already known is this instance having
  // written it itself.
  //
  if (orionldContextCacheLookup(contextId) != NULL)
  {
    KT_T(KtCoreContext, "@context '%s' is already cached - nothing to do", contextId);
    return true;
  }

  dbContextToCache(dbContextP, valueNodeP, (valueNodeP->type == KjObject), false);

  KT_T(KtCoreContext, "@context '%s' cached from the database", contextId);

  return true;
}
