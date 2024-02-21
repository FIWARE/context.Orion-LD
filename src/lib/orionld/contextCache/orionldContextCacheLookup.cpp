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

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/contextCache/orionldContextCache.h"            // Context Cache Internals
#include "orionld/contextCache/orionldContextCacheLookup.h"      // Own interface



// -----------------------------------------------------------------------------
//
// orionldContextCacheLookup -
//
OrionldContext* orionldContextCacheLookup(const char* url)
{
  OrionldContext* contextP = NULL;

  for (int ix = 0; ix < orionldContextCacheSlotIx; ix++)
  {
    if (orionldContextCache[ix] == NULL)
      continue;

    if (strcmp(url, orionldContextCache[ix]->url) == 0)
      contextP = orionldContextCache[ix];
    else if ((orionldContextCache[ix]->id != NULL) && (strcmp(url, orionldContextCache[ix]->id) == 0))
      contextP = orionldContextCache[ix];

    if (contextP != NULL)
      return contextP;
  }

  return NULL;
}
