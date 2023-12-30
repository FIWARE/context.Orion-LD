/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include "kjson/kjFree.h"                                        // kjFree
}

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/mongoc/mongocContextCacheDelete.h"             // mongocContextCacheDelete
#include "orionld/contextCache/orionldContextCache.h"            // Context Cache Internals
#include "orionld/contextCache/orionldContextCacheDelete.h"      // Own interface



// -----------------------------------------------------------------------------
//
// contextCacheReleaseOne -
//
static void contextCacheReleaseOne(OrionldContext* contextP)
{
  if (contextP->tree != NULL)
    kjFree(contextP->tree);
}



// -----------------------------------------------------------------------------
//
// orionldContextCacheDelete -
//
bool orionldContextCacheDelete(const char* id)
{
  bool found = false;

  sem_wait(&orionldContextCacheSem);

  for (int ix = 0; ix < orionldContextCacheSlotIx; ix++)
  {
    if (orionldContextCache[ix] == NULL)
      continue;

    //
    // If same id, then delete
    // Also delete any context that has the context to be deleted as parent, unless Downloaded.
    //
    if ((orionldContextCache[ix]->id != NULL) && (strcmp(id, orionldContextCache[ix]->id) == 0))
    {
      mongocContextCacheDelete(orionldContextCache[ix]->id);
      contextCacheReleaseOne(orionldContextCache[ix]);
      orionldContextCache[ix] = NULL;
      found = true;
    }
    else if ((orionldContextCache[ix]->url != NULL) && (strcmp(id, orionldContextCache[ix]->url) == 0))
    {
      mongocContextCacheDelete(orionldContextCache[ix]->id);
      contextCacheReleaseOne(orionldContextCache[ix]);
      orionldContextCache[ix] = NULL;
      found = true;
    }
    else if ((orionldContextCache[ix]->origin != OrionldContextDownloaded) && (orionldContextCache[ix]->parent != NULL) && (strcmp(id, orionldContextCache[ix]->parent) == 0))
    {
      mongocContextCacheDelete(orionldContextCache[ix]->id);
      contextCacheReleaseOne(orionldContextCache[ix]);
      orionldContextCache[ix] = NULL;
    }
  }

  sem_post(&orionldContextCacheSem);

  return found;
}
