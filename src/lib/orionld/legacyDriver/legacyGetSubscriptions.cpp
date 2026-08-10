/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include <vector>                                                // std::vector

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjArray
}

#include "cache/subCache.h"                                      // CachedSubscription, subCacheHeadGet, subCacheItemLookup
#include "apiTypesV2/Subscription.h"                             // ngsiv2::Subscription
#include "mongoBackend/mongoGetSubscriptions.h"                  // mongoListSubscriptions

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/types/OrionldHeader.h"                         // orionldHeaderAdd, HttpResultsCount
#include "orionld/subCache/subCacheItemLookup.h"                 // subCacheItemLookup
#include "orionld/legacyDriver/kjTreeFromSubscription.h"         // kjTreeFromSubscription
#include "orionld/legacyDriver/legacyGetSubscriptions.h"         // Own interface



// ----------------------------------------------------------------------------
//
// legacyGetSubscriptions -
//
bool legacyGetSubscriptions(void)
{
  std::vector<ngsiv2::Subscription> subVec;
  OrionError                        oe;
  int64_t                           count  = 0;

  mongoGetLdSubscriptions("/#", &subVec, orionldState.tenantP, (long long*) &count, &oe);

  if (orionldState.uriParams.count == true)
    orionldHeaderAdd(&orionldState.out.headers, HttpResultsCount, NULL, count);

  orionldState.responseTree = kjArray(orionldState.kjsonP, NULL);

  for (unsigned int ix = 0; ix < subVec.size(); ix++)
  {
    SubCacheItem* sciP = subCacheItemLookup(orionldState.tenantP->subCache, subVec[ix].id.c_str());

    //
    // Only what this broker has in its cache is answered - NOT everything the
    // database holds. With several brokers on one database, the subscriptions of
    // the OTHER broker are in there too, and they are not this broker's to report
    // until they have propagated into its cache.
    //
    if (sciP != NULL)
    {
      KjNode* subscriptionNodeP = kjTreeFromSubscription(&subVec[ix], sciP, orionldState.contextP);
      kjChildAdd(orionldState.responseTree, subscriptionNodeP);
    }
  }

  return true;
}
