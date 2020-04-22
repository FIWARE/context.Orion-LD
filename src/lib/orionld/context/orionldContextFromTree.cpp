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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjRender.h"                                      // kjRender
#include "kjson/kjFree.h"                                        // kjFree
}

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "orionld/common/orionldState.h"                         // orionldState, kalloc
#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails, orionldProblemDetailsFill
#include "orionld/context/OrionldContext.h"                      // OrionldContext
#include "orionld/context/orionldCoreContext.h"                  // ORIONLD_CORE_CONTEXT_URL
#include "orionld/context/orionldContextSimplify.h"              // orionldContextSimplify
#include "orionld/context/orionldContextFromUrl.h"               // orionldContextFromUrl
#include "orionld/context/orionldContextFromObject.h"            // orionldContextFromObject
#include "orionld/context/orionldContextFromArray.h"             // orionldContextFromArray
#include "orionld/context/orionldContextCacheLookup.h"           // orionldContextCacheLookup
#include "orionld/context/orionldContextCreate.h"                // orionldContextCreate
#include "orionld/context/orionldContextCacheInsert.h"           // orionldContextCacheInsert
#include "orionld/context/orionldContextFromTree.h"              // Own interface



// -----------------------------------------------------------------------------
//
// willBeSimplified -
//
bool willBeSimplified(KjNode* contextTreeP, int* itemsInArrayP)
{
  int  itemsInArray  = 0;
  int  itemsToRemove = 0;

  for (KjNode* itemP = contextTreeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    ++itemsInArray;
    if ((itemP->type == KjString) && (strcmp(itemP->value.s, ORIONLD_CORE_CONTEXT_URL) == 0))
      ++itemsToRemove;
  }

  if (itemsInArray - itemsToRemove > 1)  // More than one item in array - must stay array
    return false;

  *itemsInArrayP = itemsInArray;
  return true;
}



// -----------------------------------------------------------------------------
//
// orionldContextFromTree -
//
OrionldContext* orionldContextFromTree(char* url, bool toBeCloned, KjNode* contextTreeP, OrionldProblemDetails* pdP)
{
  int itemsInArray;

  if (contextTreeP->type == KjArray)
  {
    contextTreeP = orionldContextSimplify(contextTreeP, &itemsInArray);
    if ((contextTreeP == NULL) || (contextTreeP->value.firstChildP == NULL))
    {
      // Nothing left in  the array - only Core Context was there but has been removed?
      pdP->status = 200;

      return orionldCoreContextP;
    }

    bool insert = true;
    if (url == NULL)
    {
      url    = (char*) "no url";
      insert = false;
    }

    // Need to clone the array and add it to the cache before it is destroyed
    OrionldContext* contextP = orionldContextCreate(url, NULL, contextTreeP, false, insert);

    contextP->context.array.items     = itemsInArray;
    contextP->context.array.vector    = (OrionldContext**) kaAlloc(&kalloc, itemsInArray * sizeof(OrionldContext*));

    int ix = 0;
    for (KjNode* ctxItemP = contextTreeP->value.firstChildP; ctxItemP != NULL; ctxItemP = ctxItemP->next)
    {
      OrionldContext* cachedContextP = NULL;

      if (ctxItemP->type == KjString)
        cachedContextP = orionldContextCacheLookup(ctxItemP->value.s);
      else if ((ctxItemP->type != KjObject) && (ctxItemP->type != KjArray))
      {
        LM_E(("invalid type of @context array item: %s", kjValueType(ctxItemP->type)));
        pdP->type   = OrionldBadRequestData;
        pdP->title  = (char*) "Invalid @context - invalid type for @context array item";
        pdP->detail = (char*) kjValueType(ctxItemP->type);
        pdP->status = 400;

        if (insert == true)
          kjFree(contextP->tree);
        return NULL;
      }

      if (cachedContextP == NULL)
        contextP->context.array.vector[ix] = orionldContextFromTree(NULL, true, ctxItemP, pdP);
      else
        contextP->context.array.vector[ix] = cachedContextP;
      ++ix;
    }

    if (insert)
      orionldContextCacheInsert(contextP);

    return contextP;
  }
  else if (contextTreeP->type == KjString)
  {
    if (url != NULL)
    {
      OrionldContext* contextP = orionldContextCacheLookup(url);

      if (contextP == NULL)
      {
        contextP = orionldContextCreate(url, NULL, contextTreeP, false, true);

        contextP->context.array.items     = 1;
        contextP->context.array.vector    = (OrionldContext**) kaAlloc(&kalloc, 1 * sizeof(OrionldContext*));
        contextP->context.array.vector[0] = orionldContextFromUrl(contextTreeP->value.s, pdP);
      }
      return contextP;
    }
    else
    {
      OrionldContext* contextP;
      contextP = orionldContextFromUrl(contextTreeP->value.s, pdP);
      return contextP;
    }
  }
  else if (contextTreeP->type == KjObject)
    return orionldContextFromObject(url, toBeCloned, contextTreeP, pdP);


  //
  // None of the above. Error
  //
  pdP->type   = OrionldBadRequestData;
  pdP->title  = (char*) "Invalid type for item in @context array";
  pdP->detail = (char*) kjValueType(contextTreeP->type);
  pdP->status = 400;

  LM_E(("%s: %s", pdP->title, pdP->detail));
  return NULL;
}
