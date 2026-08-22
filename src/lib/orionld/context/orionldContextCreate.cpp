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
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/kjClone.h"                                       // kjClone
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/common/orionldState.h"                         // orionldState, kalloc
#include "orionld/common/kallocGuard.h"                          // kallocGuardedAlloc, kallocGuardedStrdup



// -----------------------------------------------------------------------------
//
// orionldContextCreate -
//
// FIXME: If the context is to be saved in the cache, then 'kalloc' can't be used.
//        Might need two version of this function, one for kaAlloc, one for malloc
//
// The arena a context is allocated from follows its lifetime:
//
// * A context WITHOUT a URL is never inserted in the context cache (see orionldContextFromTree:
//   'arrayToCache = (url != NULL)') - it lives only for the duration of the request that built it.
//   When the caller asks for it ('ephemeral'), such a context goes in the calling thread's own
//   arena, orionldState.kalloc. That keeps the hot path - an inline @context resolved once per
//   entity of every batch - entirely off the process-global 'kalloc', which is shared by all HTTP
//   and Kafka consumer threads and has no locking of its own. It also stops those per-entity
//   allocations from leaking into the global arena, which is never reset.
//
// * Everything else (cached contexts, and any context a caller may hand to something outlasting the
//   request, such as the subscription cache) stays in the global arena - allocated through
//   kallocGuard so that concurrent threads don't corrupt it.
//
int cloned = 0;
OrionldContext* orionldContextCreate(const char* url, OrionldContextOrigin origin, const char* id, KjNode* tree, bool keyValues, bool ephemeral)
{
  KAlloc*         kaP      = ((ephemeral == true) && (url == NULL))? &orionldState.kalloc : &kalloc;
  OrionldContext* contextP = (OrionldContext*) kallocGuardedAlloc(kaP, sizeof(OrionldContext));

  if (contextP == NULL)
    KT_X(1, "out of memory - trying to allocate a OrionldContext of %d bytes", sizeof(OrionldContext));

  contextP->kallocP   = kaP;
  contextP->origin    = origin;
  contextP->kind      = OrionldContextCached;  // Default. Changed later to Hosted/Implicit if needed
  contextP->parent    = NULL;
  contextP->createdAt = orionldState.requestTime;
  contextP->usedAt    = orionldState.requestTime;

  // NULL URL means NOT to be saved - will live just inside the request-thread
  if (url != NULL)
  {
    contextP->url   = kallocGuardedStrdup(kaP, url);
    contextP->id    = (id != NULL)? kallocGuardedStrdup(kaP, id) : NULL;

    //
    // If just a string, no clone needed
    //
    if (tree->type != KjString)
      ++cloned;
    contextP->tree = (tree->type != KjString)? kjClone(NULL, tree) : tree;
  }
  else
  {
    contextP->tree = tree;
    contextP->url  = NULL;
    contextP->id   = NULL;
  }

  contextP->keyValues = keyValues;
  contextP->lookups   = 1;

  return contextP;
}
