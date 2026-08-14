/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
#include <string.h>                                            // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "orionld/types/RegCache.h"                            // RegCache
#include "orionld/types/RegCacheItem.h"                        // RegCacheItem
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/regCache/regCacheSem.h"                        // regCacheSemTake, regCacheSemGive
#include "orionld/regMatch/regMatchSubscription.h"             // Own interface



// -----------------------------------------------------------------------------
//
// regMatchSubscription -
//
bool regMatchSubscription
(
  RegCacheItem*  rciP,
  KjNode*        entitiesP,
  char**         entityTypeP
)
{
  KjNode* regInfoP = kjLookup(rciP->regTree, "information");

  if (regInfoP == NULL)
    return false;

  if (entitiesP == NULL)
    return false;

  //
  // Only a TYPE-ONLY entity selector can match a registration: one that names a
  // type and selects every entity id - either by saying nothing about the id, or
  // by an idPattern of ".*", which is the same thing said out loud.
  //
  for (KjNode* selectorP = entitiesP->value.firstChildP; selectorP != NULL; selectorP = selectorP->next)
  {
    KjNode* subTypeP       = kjLookup(selectorP, "type");
    KjNode* subIdP         = kjLookup(selectorP, "id");
    KjNode* subIdPatternP  = kjLookup(selectorP, "idPattern");
    bool    anyEntityId    = (subIdP == NULL) && ((subIdPatternP == NULL) || (strcmp(subIdPatternP->value.s, ".*") == 0));

    KT_T(KtSR, "entityType: '%s', anyEntityId: %s", (subTypeP != NULL)? subTypeP->value.s : "none", anyEntityId? "true" : "false");

    if ((subTypeP != NULL) && (anyEntityId == true))
    {
      const char* entityType = subTypeP->value.s;

      //
      // We have the entity type of the subscription, now match against the registration.
      // The walk runs under the READ lock, so the list cannot change under us. Note that the
      // match below must NOT return from inside the loop - the lock has to be given back first.
      //
      bool matched = false;

      regCacheSemTake(orionldState.tenantP->regCache, __FUNCTION__, "Matching registrations for a subscription", SemReadOp);

      for (RegCacheItem* rciP = orionldState.tenantP->regCache->regList; (rciP != NULL) && (matched == false); rciP = rciP->next)
      {
        KjNode* informationP = kjLookup(rciP->regTree, "information");
        if (informationP == NULL)
          continue;

        for (KjNode* infoP = informationP->value.firstChildP; infoP != NULL; infoP = infoP->next)
        {
          KjNode* entitiesP = kjLookup(infoP, "entities");
          if (entitiesP == NULL)
            continue;

          for (KjNode* entityInfoP = entitiesP->value.firstChildP; entityInfoP != NULL; entityInfoP = entityInfoP->next)
          {
            // Only supported if only entity type is given
            KjNode* typeP = entityInfoP->value.firstChildP;

            //
            // If the first child is NOT "type", OR there's another child => NOT only entity type is given => no support
            //
            if ((strcmp(typeP->name, "type") != 0) || (typeP->next != NULL))
            {
              KT_W("For now, distributed subscriptions only work for type based registrations");
              break;
            }

            if (strcmp(entityType, typeP->value.s) == 0)
            {
              KT_T(KtSR, "Found a matching registration for entity type '%s': %s", entityType, rciP->regId);
              *entityTypeP = (char*) entityType;
              matched = true;
              break;
            }
          }

          if (matched == true)
            break;
        }
      }

      regCacheSemGive(orionldState.tenantP->regCache, __FUNCTION__, "Matching registrations for a subscription");

      if (matched == true)
        return true;
    }
  }

  return false;
}
