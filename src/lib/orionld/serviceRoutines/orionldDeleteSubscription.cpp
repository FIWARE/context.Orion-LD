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
* Author: Ken Zangelin and Gabriel Quaresma
*/
extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/kjLookup.h"                                       // KT_*
}

#include "cache/subCache.h"                                      // CachedSubscription, subCacheItemLookup, ...

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KTrace level
#include "orionld/http/httpRequest.h"                            // httpRequest
#include "orionld/payloadCheck/PCHECK.h"                         // PCHECK_URI
#include "orionld/mqtt/mqttDisconnect.h"                         // mqttDisconnect
#include "orionld/mongoc/mongocSubscriptionLookup.h"             // mongocSubscriptionLookup
#include "orionld/mongoc/mongocSubscriptionDelete.h"             // mongocSubscriptionDelete
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/subCache/subCacheItemLookup.h"                 // subCacheItemLookup (the new sub cache)
#include "orionld/subCache/subCacheItemRemove.h"                 // subCacheItemRemove (the new sub cache)
#include "orionld/legacyDriver/legacyDeleteSubscription.h"       // legacyDeleteSubscription
#include "orionld/regCache/regCacheItemLookup.h"                 // regCacheItemLookup
#include "orionld/kjTree/kjTreeLog.h"                            // KT_TREE
#include "orionld/serviceRoutines/orionldDeleteSubscription.h"   // Own Interface


// ----------------------------------------------------------------------------
//
// orionldDeleteSubscription -
//
bool orionldDeleteSubscription(void)
{
  if ((experimental == false) || (orionldState.in.legacy != NULL))
    return legacyDeleteSubscription();

  PCHECK_URI(orionldState.wildcard[0], true, 0, "Invalid Subscription Identifier", orionldState.wildcard[0], 400);

  KjNode* subP = mongocSubscriptionLookup(orionldState.wildcard[0]);
  if (subP == NULL)
  {
    orionldError(OrionldResourceNotFound, "Subscription not found", orionldState.wildcard[0], 404);
    return false;
  }

  if (mongocSubscriptionDelete(orionldState.wildcard[0]) == false)
    return false;  // mongocSubscriptionDelete calls orionldError, setting status code to 500

  //
  // The cache item is needed BEFORE it is removed: it is what says whether there
  // is an MQTT connection to close and which subordinate subscriptions to take
  // down with it.
  //
  SubCacheItem* sciP = subCacheItemLookup(orionldState.tenantP->subCache, orionldState.wildcard[0]);

  if (sciP == NULL)
  {
    if (noCache == false)
      KT_W("The subscription '%s' was successfully removed from DB but does not exist in sub-cache ... (sub-cache is enabled)", orionldState.wildcard[0]);
  }
  else
  {
    // If MQTT subscription - disconnect from mqtt broker
    if (((sciP->protocol == MQTT) || (sciP->protocol == MQTTS)) && (sciP->mqttP != NULL))
    {
      MqttInfo* mqttP = sciP->mqttP;
      mqttDisconnect(mqttP->mqtts, mqttP->host, mqttP->port, mqttP->username, mqttP->password, mqttP->version);
    }

    //
    // Any subordinate subscriptions? They live in the subTree, which is the
    // source of truth - each one names the registration it was created behind.
    //
    KjNode* subordinateArrayP = kjLookup(sciP->subTree, "subordinate");

    for (KjNode* subordinateP = (subordinateArrayP != NULL)? subordinateArrayP->value.firstChildP : NULL;
         subordinateP != NULL;
         subordinateP = subordinateP->next)
    {
      KjNode* regIdP = kjLookup(subordinateP, "registrationId");
      KjNode* subIdP = kjLookup(subordinateP, "subscriptionId");

      if ((regIdP == NULL) || (subIdP == NULL))
        continue;

      RegCacheItem* rciP = regCacheItemLookup(orionldState.tenantP->regCache, regIdP->value.s);

      //
      // The registration may be gone - it can be deleted independently of the
      // subscription that was created behind it. Nothing to send the DELETE to.
      //
      if (rciP == NULL)
      {
        KT_W("Subordinate subscription '%s': its registration '%s' is gone - cannot delete it remotely", subIdP->value.s, regIdP->value.s);
        continue;
      }

      char  url[256];
      char  ip[128];

      strncpy(ip, rciP->ipAndPort, sizeof(ip) - 1);
      ip[sizeof(ip) - 1] = 0;
      char* colon = strchr(ip, ':');
      if (colon != NULL)
        *colon = 0;

      snprintf(url, sizeof(url) - 1, "http://%s/ngsi-ld/v1/subscriptions/%s", rciP->ipAndPort, subIdP->value.s);

      KjNode*                responseTree = NULL;
      OrionldProblemDetails  pd;
      int                    r;

      bzero(&pd, sizeof(pd));
      r = httpRequest(ip, "DELETE", url, NULL, NULL, NULL, 5000, &responseTree, &pd);
      if (r != 204)
      {
        KT_W("Unable to DELETE subordinate subscription '%s': status code %d, %s: %s", subIdP->value.s, r, pd.title, pd.detail);
        KT_TREE(responseTree, "Error response payload body", KtSR);
      }
    }

    subCacheItemRemove(orionldState.tenantP->subCache, orionldState.wildcard[0]);
  }

  // Out of the legacy cache too - it still serves the NGSIv2 matching path
  CachedSubscription* cSubP = subCacheItemLookup(orionldState.tenantP->tenant, orionldState.wildcard[0]);
  if (cSubP != NULL)
    subCacheItemRemove(cSubP);

  orionldState.httpStatusCode = 204;

  return true;
}
