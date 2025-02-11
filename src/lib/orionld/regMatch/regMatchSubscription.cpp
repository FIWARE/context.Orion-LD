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
extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "cache/CachedSubscription.h"                          // CachedSubscription

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/types/RegCache.h"                            // RegCache
#include "orionld/types/RegCacheItem.h"                        // RegCacheItem
#include "orionld/regMatch/regMatchSubscription.h"             // Own interface



// -----------------------------------------------------------------------------
//
// regMatchSubscription -
//
bool regMatchSubscription
(
  RegCacheItem*       rciP,
  CachedSubscription* cSubP,
  char**              entityTypeP
)
{
  KjNode* regInfoP = kjLookup(rciP->regTree, "information");

  if (regInfoP == NULL)
    return false;

  for (unsigned long ix = 0; ix < cSubP->entityIdInfos.size(); ix++)
  {
    EntityInfo* eiP = cSubP->entityIdInfos[ix];

    // For now, only match subs/regs with entity type only
    LM_T(LmtSR, ("entityType  : '%s', entityId: '%s'", eiP->entityType.c_str(), eiP->entityId.c_str()));
    if ((eiP->entityType != "") && (eiP->entityId == ".*"))
    {
      const char* entityType = eiP->entityType.c_str();

      // We have the entity type of the subscription, now match against the registration
      for (RegCacheItem* rciP = orionldState.tenantP->regCache->regList; rciP != NULL; rciP = rciP->next)
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
              LM_W(("For now, distributed subscriptions only work for type based registrations"));
              break;
            }

            if (strcmp(entityType, typeP->value.s) == 0)
            {
              LM_T(LmtSR, ("Found a matching registration for entity type '%s': %s", entityType, rciP->regId));
              *entityTypeP = (char*) entityType;
              return true;
            }
          }
        }
      }
    }
  }

  return false;
}
