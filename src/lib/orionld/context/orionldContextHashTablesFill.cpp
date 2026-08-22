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
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "khash/khash.h"                                         // KHashTable, KHashListItem, khashItemAdd, ...
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails, orionldProblemDetailsFill
#include "orionld/types/OrionldContextItem.h"                    // OrionldContextItem
#include "orionld/types/OrionldContext.h"                        // OrionldContext, OrionldContextHashTables
#include "orionld/common/orionldState.h"                         // orionldState, kalloc
#include "orionld/common/kallocGuard.h"                          // kallocGuardTake, kallocGuardGive
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/contextCache/orionldContextCache.h"            // ORIONLD_CONTEXT_CACHE_HASH_ARRAY_SIZE
#include "orionld/context/orionldContextPrefixExpand.h"          // orionldContextPrefixExpand
#include "orionld/context/orionldContextHashTablesFill.h"        // Own interface



// -----------------------------------------------------------------------------
//
// hashTablesFill - the body of orionldContextHashTablesFill
//
// Everything here is allocated from contextP->kallocP - either directly, or indirectly via
// khashItemAdd, which allocates from the arena its hash table was created with. The caller holds the
// kalloc guard for the whole duration, so the early returns don't have to release anything.
//
static bool hashTablesFill(OrionldContext* contextP, KjNode* keyValueTree, OrionldProblemDetails* pdP)
{
  KAlloc*                   kaP             = contextP->kallocP;
  OrionldContextHashTables* hashP           = &contextP->context.hash;
  KHashTable*               nameHashTableP  = hashP->nameHashTable;
  KHashTable*               valueHashTableP = hashP->valueHashTable;

  for (KjNode* kvP = keyValueTree->value.firstChildP; kvP != NULL; kvP = kvP->next)
  {
    //
    // Two new fields, neither String nor Object were introduced in the Core Context version 1.8
    //
    if (strcmp(kvP->name, "@version") == 0)
    {
      // FIXME: Keep the JSON-LD version ?
      continue;
    }
    else if (strcmp(kvP->name, "@protected") == 0)
    {
      // FIXME: Keep the JSON-LD protection bool ?
      continue;
    }

    OrionldContextItem* hiP = (OrionldContextItem*) kaAlloc(kaP, sizeof(OrionldContextItem));

    hiP->name = kaStrdup(kaP, kvP->name);
    hiP->type = NULL;

    if (kvP->type == KjString)
      hiP->id = kvP->value.s;  // Will be allocated in pass II
    else if (kvP->type == KjObject)
    {
      hiP->id = NULL;

      //
      // Find @id, @type
      //
      for (KjNode* itemP = kvP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      {
        if (strcmp(itemP->name, "@id") == 0)
          hiP->id = itemP->value.s;  // Will be allocated in pass II
        else if (strcmp(itemP->name, "@type") == 0)
          hiP->type = kaStrdup(kaP, itemP->value.s);
      }
    }
    else
    {
      KT_W("Bad Input (invalid value type for '%s': %s)", kvP->name, kjValueType(kvP->type));
      orionldProblemDetailsFill(pdP, OrionldBadRequestData, "Invalid key-value in @context", kvP->name, 400);
      return false;
    }

    if ((hiP->id == NULL) || (hiP->id[0] == 0))
    {
      KT_W("Bad Input (NULL value for key '%s')", kvP->name);

      pdP->type   = OrionldBadRequestData;
      pdP->title  = (char*) "NULL value for key in context";
      pdP->detail = (char*) kvP->name;
      pdP->status = 400;

      return false;
    }

    // KT_T(KtContextItem, "Adding '%s' -> '%s' to hash table for context '%s' (step 1)", hiP->name, hiP->id, contextP->url);
    khashItemAdd(nameHashTableP,  hiP->name, hiP);
  }


  //
  // Second pass, to fix prefix expansion in the values, and to create the valueHashTable
  // In this pass, the 'id' (value) is copied from the request thread's arena (that's where
  // orionldContextPrefixExpand puts it) into the context's own arena
  //
  for (int slot = 0; slot < ORIONLD_CONTEXT_CACHE_HASH_ARRAY_SIZE; ++slot)
  {
    KHashListItem* itemP = nameHashTableP->array[slot];

    while (itemP != NULL)
    {
      //
      // Expand if the value contains a colon
      //
      OrionldContextItem* hashItemP = (OrionldContextItem*) itemP->data;
      char*               colonP    = strchr(hashItemP->id, ':');

      if (colonP != NULL)
        hashItemP->id = orionldContextPrefixExpand(contextP, hashItemP->id, colonP);

      hashItemP->id = kaStrdup(kaP, hashItemP->id);
      khashItemAdd(valueHashTableP, hashItemP->id, hashItemP);

      // KT_T(KtContextItem, "Fixed '%s' -> '%s' in hash table for context '%s' (step 2)", hashItemP->name, hashItemP->id, contextP->url);

      itemP = itemP->next;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// orionldContextHashTablesFill -
//
bool orionldContextHashTablesFill(OrionldContext* contextP, KjNode* keyValueTree, OrionldProblemDetails* pdP)
{
  kallocGuardTake(contextP->kallocP);
  bool ok = hashTablesFill(contextP, keyValueTree, pdP);
  kallocGuardGive(contextP->kallocP);

  return ok;
}
