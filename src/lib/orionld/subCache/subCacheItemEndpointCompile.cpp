/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include <string.h>                                              // strdup

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldMimeType.h"                       // MimeType, mimeTypeFromString
#include "orionld/types/Protocol.h"                              // Protocol, protocolFromString
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/urlParse.h"                             // urlParse
#include "orionld/subCache/subCacheItemEndpointCompile.h"        // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemEndpointCompile -
//
void subCacheItemEndpointCompile(SubCacheItem* sciP, KjNode* endpointP)
{
  KjNode* uriP    = kjLookup(endpointP, "uri");
  KjNode* acceptP = kjLookup(endpointP, "accept");

  //
  // "accept" is optional, "uri" is not - pCheckSubscription makes sure of that,
  // but a subscription straight from the database has seen no pCheckSubscription.
  //
  // mimeTypeFromString cuts its input at a ';' (charset) and needs the accept-mask
  // output parameter for wildcards - so it gets a copy of the string, not the one
  // inside the cached subTree.
  //
  if (acceptP != NULL)
  {
    char      accept[64];
    uint32_t  acceptMask = 0;

    strncpy(accept, acceptP->value.s, sizeof(accept) - 1);
    accept[sizeof(accept) - 1] = 0;

    sciP->mimeType = mimeTypeFromString(accept, NULL, true, false, &acceptMask);
  }
  else
    sciP->mimeType = MT_JSON;

  if (uriP == NULL)
    KT_RVE("Sub '%s': no 'notification::endpoint::uri' - the subscription can never notify", sciP->subId);

  //
  // urlParse destroys its input and hands back pointers into it - so it gets a
  // copy of the URI that the cache item owns.
  //
  sciP->url = strdup(uriP->value.s);

  if (urlParse(sciP->url, &sciP->protocolString, &sciP->ip, &sciP->port, &sciP->rest) == false)
    KT_RVE("Sub '%s': invalid 'notification::endpoint::uri' ('%s')", sciP->subId, uriP->value.s);

  sciP->protocol = protocolFromString(sciP->protocolString);

  KT_T(KtSubCache, "Sub '%s': endpoint protocol: '%s', IP: '%s', port: %d, rest: '%s'",
       sciP->subId, sciP->protocolString, sciP->ip, sciP->port, sciP->rest);
}
