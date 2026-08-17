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
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjFree.h"                                        // kjFree
}

#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails, orionldProblemDetailsFill
#include "orionld/types/OrionldContextItem.h"                    // OrionldContextItem
#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/common/orionldState.h"                         // kalloc, orionldState
#include "orionld/context/orionldContextCreate.h"                // orionldContextCreate
#include "orionld/context/orionldContextUrlGenerate.h"           // orionldContextUrlGenerate
#include "orionld/contextCache/orionldContextCache.h"            // ORIONLD_CONTEXT_CACHE_HASH_ARRAY_SIZE
#include "orionld/contextCache/orionldContextCacheInsert.h"      // orionldContextCacheInsert
#include "orionld/context/orionldContextHashTablesFill.h"        // orionldContextHashTablesFill
#include "orionld/common/kallocGuard.h"                          // kallocGuardTake, kallocGuardGive
#include "orionld/context/orionldContextFromObject.h"            // Own interface



// -----------------------------------------------------------------------------
//
// hashCode -
//
unsigned int hashCode(const char* name)
{
  unsigned int code = 0;

  while (*name != 0)
  {
    code += (unsigned char) *name;
    ++name;
  }

  return code;
}



// -----------------------------------------------------------------------------
//
// nameCompareFunction -
//
static int nameCompareFunction(const char* name, void* itemP)
{
  OrionldContextItem* cItemP = (OrionldContextItem*) itemP;

  return strcmp(name, cItemP->name);
}



// ----------------------------------------------------------------------------
//
// valueCompareFunction -
//
static int valueCompareFunction(const char* longname, void* itemP)
{
  OrionldContextItem* cItemP = (OrionldContextItem*) itemP;

  return strcmp(longname, cItemP->id);
}



// -----------------------------------------------------------------------------
//
// orionldContextFromObject -
//
// If the context object 'contextObjectP' is part of an array, then it's a local context and
// it is not served.
// Served contexts need to be cloned so that they can be copied back to the caller (GET /ngsi-ld/ex/contexts/xxx).
// For example, the URL "http:/x.y.z/contexts/context1.jsonld" was downloaded and its content is a key-value object.
//
OrionldContext* orionldContextFromObject
(
  char*                   url,
  OrionldContextOrigin    origin,
  char*                   id,
  KjNode*                 contextObjectP,
  bool                    ephemeral
)
{
  OrionldContext*  contextP;
  bool             ok = true;

  contextP = orionldContextCreate(url, origin, id, contextObjectP, true, ephemeral);
  if (contextP == NULL)
    KT_RE(NULL, "orionldContextCreate failed");

  //
  // The hash tables (and every item later added to them) are allocated from the context's own arena.
  // khashTableCreate/khashItemAdd allocate internally, so the guard is taken around the calls
  // instead of via kallocGuardedAlloc. It is a no-op for a thread-local arena.
  //
  kallocGuardTake(contextP->kallocP);

  contextP->context.hash.nameHashTable  = khashTableCreate(contextP->kallocP, hashCode, nameCompareFunction,  ORIONLD_CONTEXT_CACHE_HASH_ARRAY_SIZE);
  if (contextP->context.hash.nameHashTable == NULL)
  {
    KT_E("khashTableCreate failed");
    ok = false;
  }

  contextP->context.hash.valueHashTable = khashTableCreate(contextP->kallocP, hashCode, valueCompareFunction, ORIONLD_CONTEXT_CACHE_HASH_ARRAY_SIZE);
  if (contextP->context.hash.valueHashTable == NULL)
  {
    KT_E("khashTableCreate failed");
    ok = false;
  }

  kallocGuardGive(contextP->kallocP);

  if ((ok == true) && (orionldContextHashTablesFill(contextP, contextObjectP, &orionldState.pd) == false))
  {
    // orionldContextHashTablesFill fills in pdP
    KT_E("orionldContextHashTablesFill failed");
    ok = false;
  }

  if (ok == false)
  {
    if (url != NULL)  // If URL present, the tree of the context has been cloned by orionldContextCreate
      kjFree(contextP->tree);
    return NULL;
  }

  if (url != NULL)
    orionldContextCacheInsert(contextP);

  return contextP;
}
