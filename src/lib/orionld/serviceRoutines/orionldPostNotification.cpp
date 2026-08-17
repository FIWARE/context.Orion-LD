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
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjChildRemove
}

#include "orionld/types/SubCacheItem.h"                        // SubCacheItem
#include "orionld/subCache/subCacheItemLookup.h"               // subCacheItemLookup
#include "orionld/types/HttpKeyValue.h"                        // HttpKeyValue
#include "orionld/types/OrionLdRestService.h"                  // OrionLdRestService
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldError.h"                       // orionldError
#include "orionld/common/traceLevels.h"                        // KTrace level
#include "orionld/http/httpRequest.h"                          // httpRequest
#include "orionld/http/httpRequestHeaderAdd.h"                 // httpRequestHeaderAdd
#include "orionld/kjTree/kjTreeLog.h"                          // KT_TREE
#include "orionld/serviceRoutines/orionldPostNotification.h"   // Own interface


// ----------------------------------------------------------------------------
//
// orionldPostNotification -
//
bool orionldPostNotification(void)
{
  char* parentSubId = orionldState.wildcard[0];

  if (distSubsEnabled == false)
  {
    KT_W("Got a notification on remote subscription subordinate to '%s', but, distributed subscriptions are not enabled", parentSubId);

    orionldError(OrionldOperationNotSupported, "Distributed Subscriptions Are Not Enabled", orionldState.serviceP->url, 501);
    orionldState.noLinkHeader   = true;  // We don't want the Link header for non-implemented requests

    return true;
  }

  KT_T(KtSubordinate, "Got a notification on remote subscription subordinate to '%s'", parentSubId);

  KT_TREE(orionldState.requestTree, "notification", KtSubordinate);

  SubCacheItem* sciP = subCacheItemLookup(orionldState.tenantP->subCache, parentSubId);
  if (sciP == NULL)
  {
    KT_W("Got a notification from a remote subscription '%s' on IP:PORT, but, its local parent subscription was not found", parentSubId);
    return false;
  }

  // Modify the payload body to fit the "new" notification triggered
  KjNode* subIdNodeP = kjLookup(orionldState.requestTree, "subscriptionId");
  if (subIdNodeP != NULL)
    subIdNodeP->value.s = parentSubId;

  // Send the notification
  OrionldProblemDetails pd;
  char                  url[256];
  KjNode*               responseTree = NULL;
  int                   httpStatus;
  HttpKeyValue          uriParams[2];
  HttpKeyValue          headers[20];
  int                   headerIx = 0;

  bzero(&uriParams, sizeof(uriParams));
  bzero(&headers, sizeof(headers));

  uriParams[0].key   = (char*) "subscriptionId";
  uriParams[0].value = (char*) parentSubId;

  snprintf(url, sizeof(url), "%s://%s:%d/%s", sciP->protocolString, sciP->ip, sciP->port, sciP->rest);
  KT_T(KtSubordinate, "ip:  '%s'", sciP->ip);
  KT_T(KtSubordinate, "url: '%s'", url);


  //
  // HTTP Headers
  //
  KT_TREE(orionldState.in.httpHeaders, "httpHeaders", KtSubordinate);


  //
  // Content-Type - hardcoded to application/json ...  (FIXME)
  //
  httpRequestHeaderAdd(&headers[0], "Content-Type", "application/json", 0);
  headerIx = 1;


  //
  // Link - not present if application/ld+json (FIXME)
  //
  KjNode* linkP = kjLookup(orionldState.in.httpHeaders, "Link");
  if (linkP != NULL)
  {
    kjChildRemove(orionldState.in.httpHeaders, linkP);
    httpRequestHeaderAdd(&headers[headerIx], "Link", linkP->value.s, 0);
    ++headerIx;
  }

  //
  // NGSILD-Tenant
  //
  KjNode* tenantP = kjLookup(orionldState.in.httpHeaders, "NGSILD-Tenant");
  if (tenantP != NULL)
  {
    kjChildRemove(orionldState.in.httpHeaders, tenantP);
    httpRequestHeaderAdd(&headers[headerIx], "NGSILD-Tenant", linkP->value.s, 0);
    ++headerIx;
  }

  //
  // Headers from "receiverInfo" - straight out of the subscription tree, which is
  // the source of truth for everything the notification path needs
  //
  KjNode* notificationP = kjLookup(sciP->subTree, "notification");
  KjNode* endpointP     = (notificationP != NULL)? kjLookup(notificationP, "endpoint")     : NULL;
  KjNode* receiverInfoP = (endpointP     != NULL)? kjLookup(endpointP,     "receiverInfo") : NULL;

  for (KjNode* kvP = (receiverInfoP != NULL)? receiverInfoP->value.firstChildP : NULL; kvP != NULL; kvP = kvP->next)
  {
    KjNode* keyP   = kjLookup(kvP, "key");
    KjNode* valueP = kjLookup(kvP, "value");

    if ((keyP == NULL) || (valueP == NULL))
      continue;

    const char* key    = keyP->value.s;
    char*       value  = valueP->value.s;

    if (headerIx >= 19)
      KT_W("Too many headers (change and recompile for more than 20 headers) - skipping '%s'", key);
    else
    {
      KjNode* inHeaderP = kjLookup(orionldState.in.httpHeaders, key);

      if (strcmp(value, "urn:ngsi-ld:request") == 0)
      {
        if (inHeaderP != NULL)
          httpRequestHeaderAdd(&headers[headerIx], key, inHeaderP->value.s, 0);
      }
      else
        httpRequestHeaderAdd(&headers[headerIx], key, value, 0);
    }

    ++headerIx;
  }

  httpStatus = httpRequest(sciP->ip, "POST", url, orionldState.requestTree, uriParams, headers, 5000, &responseTree, &pd);
  if (httpStatus != 200)
  {
    KT_W("httpRequest for a forwarded notification gave HTTP status %d", httpStatus);
    KT_TREE(responseTree, "forwarded notification response body", KtSubordinate);
  }

  return true;
}
