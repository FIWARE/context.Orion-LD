/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include <stdlib.h>                                              // free
#include <regex.h>                                               // regfree

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjFree.h"                                        // kjFree
}

#include "orionld/types/RegCacheItem.h"                          // RegCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/regCache/regCachePresent.h"                    // regCacheList
#include "orionld/regCache/regCacheItemRegexRelease.h"           // regCacheItemRegexRelease
#include "orionld/regCache/regCacheItemFree.h"                   // regCacheItemFree
#include "orionld/regCache/regCacheSem.h"                        // regCacheSemTake, regCacheSemGive
#include "orionld/regCache/regCacheItemRemove.h"                 // Own interface


// -----------------------------------------------------------------------------
//
// regCacheItemRemove -
//
// THE REGISTRATION CACHE IS MEANT FOR ENTITY UPDATES AND RETRIEVAL FASTER, NOT FOR MAINTAINING REGISTRATIONS.
//
// It's OK for this function to be slow.
// It will rarely be used.
//
// The function would be much faster if the Registration id was part of RegCacheItem, but ...
// that would require more RAM.
// Or even, if we used a hash-table for faster lookups.
//
// BUT, looking up an individual registration for patching, deletion or whatever is FAR FROM the
// main use of the registration cache.
//
bool regCacheItemRemove(RegCache* rcP, const char* regId)
{
  if (rcP == NULL)
    KT_RE(false, "NULL rcP - that's a SW bug!");

  KT_T(KtRegCache, "Removing the reg '%s' from the regCache for tenant '%s'", regId, rcP->tenantP->mongoDbName);

  //
  // The lock is taken BEFORE the first read of rcP->regList and given back on both ways out.
  //
  // An item that a DistOp is still holding (DistOp::regP, for a forwarded request in flight) is
  // unlinked here but NOT freed - see the pin/unpin comment further down and in regCacheSem.h.
  //
  regCacheSemTake(rcP, __FUNCTION__, "Removing an item from the registration cache", SemWriteOp);

  RegCacheItem* rciP = rcP->regList;
  RegCacheItem* prev = NULL;

  regCacheList(rcP, "Before remove");

  while (rciP != NULL)
  {
    KjNode* idP = kjLookup(rciP->regTree, "id");

    if ((idP != NULL) && (strcmp(idP->value.s, regId) == 0))
    {
      // Remove rciP from rcP
      if (rciP == rcP->regList)  // First item is the item to remove - step over it
      {
        rcP->regList = rciP->next;
        if (rcP->regList == NULL)
          rcP->last = NULL;
      }
      else if (rciP->next == NULL)  // Last item is the item to remove
      {
        prev->next = NULL;    // End the list right there, just before the last item
        rcP->last  = prev;    //  And make the that "p`rev" item the last one
      }
      else  // In the middle
        prev->next = rciP->next;  // Just step over it

      //
      // The item is out of the list. Freeing it is another matter: a DistOp may still be holding
      // it for a forwarded request that is in flight right now (DistOp::regP). If so, leave it to
      // the last holder - regCacheItemUnpin does the freeing when it drops the final reference.
      //
      RegCacheItem* toFree = NULL;

      if (rciP->refs > 0)
      {
        rciP->removed = true;
        KT_T(KtRegCache, "Reg '%s' is still held by %u forwarded request(s) - freeing it on the last unpin", regId, rciP->refs);
      }
      else
        toFree = rciP;

      regCacheList(rcP, "After successful remove");
      regCacheSemGive(rcP, __FUNCTION__, "Removing an item from the registration cache");

      // Freed outside the lock - the item is unlinked, so it is ours alone
      if (toFree != NULL)
        regCacheItemFree(toFree);

      return true;
    }

    prev = rciP;
    rciP = rciP->next;
  }

  regCacheList(rcP, "After failed remove");
  regCacheSemGive(rcP, __FUNCTION__, "Removing an item from the registration cache");

  return false;
}
