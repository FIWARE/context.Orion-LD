/*
*
 Copyright 2024 FIWARE Foundation e.V.
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
#include <string>                                              // std::string
#include <map>                                                 // std::map

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjChildRemove
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "cache/CachedSubscription.h"                          // CachedSubscription
#include "cache/subCache.h"                                    // subCacheItemLookup

#include "orionld/types/HttpKeyValue.h"                        // HttpKeyValue
#include "orionld/types/OrionLdRestService.h"                  // OrionLdRestService
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldError.h"                       // orionldError
#include "orionld/http/httpRequest.h"                          // httpRequest
#include "orionld/http/httpRequestHeaderAdd.h"                 // httpRequestHeaderAdd
#include "orionld/serviceRoutines/orionldPostNotification.h"   // Own interface


#if 0
// -----------------------------------------------------------------------------
//
// subParentLookup -
//
static CachedSubscription* subParentLookup(char* subordinateSubId)
{
  LM_T(LmtSubordinate, ("Looking for subordinate subscription '%s' in the subscription cache", subordinateSubId));
  for (CachedSubscription* cSubP = subCacheHeadGet(); cSubP != NULL; cSubP = cSubP->next)
  {
    LM_T(LmtSubordinate, ("Checking subscription '%s' to see if it's the parent", cSubP->subscriptionId));
    if (cSubP->subordinateP == NULL)
    {
      LM_T(LmtSubordinate, ("It's not - no subordinates for '%s'", cSubP->subscriptionId));
      continue;
    }

    for (SubordinateSubscription* subordinateP = cSubP->subordinateP; subordinateP != NULL; subordinateP = subordinateP->next)
    {
      LM_T(LmtSubordinate, ("Comparing '%s' with '%s' of parent '%s'", subordinateP->subscriptionId, subordinateSubId, cSubP->subscriptionId));
      if (strcmp(subordinateP->subscriptionId, subordinateSubId) == 0)
      {
        LM_T(LmtSubordinate, ("Found it!"));
        return cSubP;
      }
    }
  }

  LM_T(LmtSubordinate, ("No parent subscription found"));
  return NULL;
}
#endif



// ----------------------------------------------------------------------------
//
// orionldPostNotification -
//
bool orionldPostNotification(void)
{
  char* parentSubId = orionldState.wildcard[0];

  if (distSubsEnabled == false)
  {
    LM_W(("Got a notification on remote subscription subordinate to '%s', but, distributed subscriptions are not enabled", parentSubId));

    orionldError(OrionldOperationNotSupported, "Distributed Subscriptions Are Not Enabled", orionldState.serviceP->url, 501);
    orionldState.noLinkHeader   = true;  // We don't want the Link header for non-implemented requests

    return true;
  }

  LM_T(LmtSubordinate, ("Got a notification on remote subscription subordinate to '%s'", parentSubId));

  kjTreeLog(orionldState.requestTree, "notification", LmtSubordinate);

  CachedSubscription* cSubP = subCacheItemLookup(orionldState.tenantP->tenant, parentSubId);
  if (cSubP == NULL)
  {
    LM_W(("Got a notification from a remote subscription '%s' on IP:PORT, but, its local parent subscription was not found", parentSubId));
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

  snprintf(url, sizeof(url), "%s://%s:%d/%s", cSubP->protocolString, cSubP->ip, cSubP->port, cSubP->rest);
  LM_T(LmtSubordinate, ("ip:  '%s'", cSubP->ip));
  LM_T(LmtSubordinate, ("url: '%s'", url));


  //
  // HTTP Headers
  //
  kjTreeLog(orionldState.in.httpHeaders, "httpHeaders", LmtSubordinate);


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

  // Headers from "receiverInfo" (differently stored in CachedSubscription)
  for (std::map<std::string, std::string>::const_iterator it = cSubP->httpInfo.headers.begin(); it != cSubP->httpInfo.headers.end(); ++it)
  {
    const char* key    = it->first.c_str();
    char*       value  = (char*) it->second.c_str();

    if (headerIx >= 19)
      LM_W(("Too many headers (change and recompile for more than 20 headers) - skipping '%s'", key));
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

  httpStatus = httpRequest(cSubP->ip, "POST", url, orionldState.requestTree, uriParams, headers, 5000, &responseTree, &pd);
  if (httpStatus != 200)
  {
    LM_W(("httpRequest for a forwarded notification gave HTTP status %d", httpStatus));
    kjTreeLog(responseTree, "forwarded notification response body", LmtSubordinate);
  }

  return true;
}
