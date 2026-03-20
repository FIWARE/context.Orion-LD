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
#include <stdlib.h>                                            // malloc, free
#include <string.h>                                            // strdup, strcmp, strncpy

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjClone.h"                                     // kjClone
#include "kjson/kjFree.h"                                      // kjFree
#include "kjson/kjBuilder.h"                                   // kjString, kjChildAdd
#include "kjson/kjChildCount.h"                                // kjChildCount
}

#include "orionld/types/PernotSubscription.h"                  // PernotSubscription
#include "orionld/types/QNode.h"                               // QNode
#include "orionld/types/OrionldRenderFormat.h"                 // OrionldRenderFormat
#include "common/RenderFormat.h"                               // stringToRenderFormat
#include "orionld/types/OrionldMimeType.h"                     // MimeType
#include "orionld/types/Protocol.h"                            // protocolFromString
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/urlParse.h"                           // urlParse
#include "orionld/common/dateTime.h"                           // dateTimeFromString
#include "orionld/context/orionldAttributeExpand.h"            // orionldAttributeExpand
#include "orionld/q/qRelease.h"                                // qRelease
#include "orionld/payloadCheck/pcheckGeoQ.h"                   // pcheckGeoQ
#include "orionld/pernot/pernotSubCacheUpdate.h"               // Own interface



// -----------------------------------------------------------------------------
//
// receiverInfoUpdate -
//
static void receiverInfoUpdate(PernotSubscription* pSubP, KjNode* endpointP)
{
  KjNode* receiverInfoP = kjLookup(endpointP, "receiverInfo");

  if (receiverInfoP == NULL)
    return;

  // Free old headers
  for (int ix = 0; ix < pSubP->headers.items; ix++)
    free(pSubP->headers.array[ix]);
  if (pSubP->headers.items > 0)
    free(pSubP->headers.array);

  pSubP->headers.items = 0;
  pSubP->headers.array = NULL;

  pSubP->headers.items = kjChildCount(receiverInfoP);
  if (pSubP->headers.items > 0)
  {
    pSubP->headers.array = (char**) malloc(pSubP->headers.items * sizeof(char*));

    int ix = 0;
    for (KjNode* kvPairP = receiverInfoP->value.firstChildP; kvPairP != NULL; kvPairP = kvPairP->next)
    {
      KjNode* keyP   = kjLookup(kvPairP, "key");
      KjNode* valueP = kjLookup(kvPairP, "value");
      int     sLen   = strlen(keyP->value.s) + strlen(valueP->value.s) + 4;
      char*   s      = (char*) malloc(sLen);

      snprintf(s, sLen - 1, "%s:%s\r\n", keyP->value.s, valueP->value.s);
      pSubP->headers.array[ix] = s;
      ++ix;
    }
  }
}



// -----------------------------------------------------------------------------
//
// pernotSubCacheUpdate -
//
bool pernotSubCacheUpdate
(
  PernotSubscription*  pSubP,
  KjNode*              patchBody,
  QNode*               qNodeP,
  KjNode*              geoCoordinatesP,
  double               timeInterval
)
{
  if (pSubP == NULL)
    KT_RE(false, "Internal Error (pSubP is NULL)");

  // Pause the subscription so the pernot loop skips it during update
  PernotState savedState = pSubP->state;
  pSubP->state = SubPaused;

  for (KjNode* itemP = patchBody->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    KT_T(KtPernot, "Patching pernot subscription fragment '%s'", itemP->name);

    if (strcmp(itemP->name, "timeInterval") == 0)
    {
      if (itemP->type == KjInt)
        pSubP->timeInterval = (double) itemP->value.i;
      else if (itemP->type == KjFloat)
        pSubP->timeInterval = itemP->value.f;
    }
    else if (strcmp(itemP->name, "entities") == 0)
    {
      // eSelector points into kjSubP - will be re-pointed after kjSubP replacement below
    }
    else if (strcmp(itemP->name, "isActive") == 0)
    {
      if (itemP->value.b == true)
      {
        pSubP->isActive = true;
        savedState      = SubActive;
      }
      else
      {
        pSubP->isActive = false;
        savedState      = SubPaused;
      }
    }
    else if ((strcmp(itemP->name, "expires") == 0) || (strcmp(itemP->name, "expiresAt") == 0))
    {
      char   errorString[256];
      double expiresAt = dateTimeFromString(itemP->value.s, errorString, sizeof(errorString));

      if (expiresAt > 0)
        pSubP->expiresAt = expiresAt;
    }
    else if (strcmp(itemP->name, "lang") == 0)
    {
      // lang points into kjSubP - will be re-pointed after kjSubP replacement below
    }
    else if (strcmp(itemP->name, "notification") == 0)
    {
      KjNode* formatP    = kjLookup(itemP, "format");
      KjNode* endpointP  = kjLookup(itemP, "endpoint");

      if (formatP != NULL)
      {
        pSubP->renderFormat = stringToRenderFormat(formatP->value.s);
        pSubP->ngsiv2       = (pSubP->renderFormat >= RF_CROSS_APIS_NORMALIZED);
      }

      if (endpointP != NULL)
      {
        KjNode* uriP    = kjLookup(endpointP, "uri");
        KjNode* acceptP = kjLookup(endpointP, "accept");

        if (uriP != NULL)
        {
          strncpy(pSubP->url, uriP->value.s, sizeof(pSubP->url) - 1);
          urlParse(pSubP->url, &pSubP->protocolString, &pSubP->ip, &pSubP->port, &pSubP->rest);
          pSubP->protocol = protocolFromString(pSubP->protocolString);
        }

        if (acceptP != NULL)
        {
          if      (strcmp(acceptP->value.s, "application/json")     == 0) pSubP->mimeType = MT_JSON;
          else if (strcmp(acceptP->value.s, "application/ld+json")  == 0) pSubP->mimeType = MT_JSONLD;
          else if (strcmp(acceptP->value.s, "application/geo+json") == 0) pSubP->mimeType = MT_GEOJSON;
        }

        receiverInfoUpdate(pSubP, endpointP);
      }
    }
    else if (strcmp(itemP->name, "q") == 0)
    {
      // q/qSelector handled below
    }
    else if (strcmp(itemP->name, "geoQ") == 0)
    {
      // geoSelector handled below
    }
  }

  //
  // Update qSelector
  //
  if (qNodeP != NULL)
  {
    if (pSubP->qSelector != NULL)
      qRelease(pSubP->qSelector);
    pSubP->qSelector = qNodeP;
  }

  //
  // Update geoSelector
  //
  KjNode* geoqP = kjLookup(patchBody, "geoQ");
  if (geoqP != NULL)
  {
    // Free old geoSelector
    if (pSubP->geoSelector != NULL)
    {
      if (pSubP->geoSelector->coordinates != NULL)
        kjFree(pSubP->geoSelector->coordinates);
      free(pSubP->geoSelector);
      pSubP->geoSelector = NULL;
    }

    pSubP->geoSelector = pcheckGeoQ(NULL, geoqP, true);
    if (pSubP->geoSelector != NULL)
    {
      if (pSubP->geoSelector->geoProperty == NULL)
        pSubP->geoSelector->geoProperty = (char*) "location";
      if (pSubP->geoSelector->coordinates != NULL)
        pSubP->geoSelector->coordinates = kjClone(NULL, pSubP->geoSelector->coordinates);
    }
  }

  //
  // Replace kjSubP with a fresh clone from the patchBody merged into the old kjSubP
  // We clone the patch body and merge it into the existing kjSubP
  //
  // Actually, we apply each patch field into the existing kjSubP tree
  //
  for (KjNode* itemP = patchBody->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (strcmp(itemP->name, "notification") == 0)
    {
      // Merge notification sub-fields into existing notification node
      KjNode* existingNotifP = kjLookup(pSubP->kjSubP, "notification");
      if (existingNotifP != NULL)
      {
        for (KjNode* notifItemP = itemP->value.firstChildP; notifItemP != NULL; notifItemP = notifItemP->next)
        {
          if (strcmp(notifItemP->name, "endpoint") == 0)
          {
            KjNode* existingEndpointP = kjLookup(existingNotifP, "endpoint");
            if (existingEndpointP != NULL)
            {
              for (KjNode* epItemP = notifItemP->value.firstChildP; epItemP != NULL; epItemP = epItemP->next)
              {
                KjNode* clonedP = kjClone(NULL, epItemP);
                KjNode* oldP    = kjLookup(existingEndpointP, epItemP->name);
                if (oldP != NULL)
                {
                  kjChildRemove(existingEndpointP, oldP);
                  kjFree(oldP);
                }
                kjChildAdd(existingEndpointP, clonedP);
              }
            }
          }
          else
          {
            KjNode* clonedP = kjClone(NULL, notifItemP);
            KjNode* oldP    = kjLookup(existingNotifP, notifItemP->name);
            if (oldP != NULL)
            {
              kjChildRemove(existingNotifP, oldP);
              kjFree(oldP);
            }
            kjChildAdd(existingNotifP, clonedP);
          }
        }
      }
    }
    else if ((strcmp(itemP->name, "isActive") != 0) && (strcmp(itemP->name, "status") != 0))
    {
      // isActive and status are not stored in kjSubP - they are added at render time from pSubP->isActive/state
      KjNode* clonedP = kjClone(NULL, itemP);
      KjNode* oldP    = kjLookup(pSubP->kjSubP, itemP->name);
      if (oldP != NULL)
      {
        kjChildRemove(pSubP->kjSubP, oldP);
        kjFree(oldP);
      }
      kjChildAdd(pSubP->kjSubP, clonedP);
    }
  }

  //
  // Re-point selectors into the updated kjSubP tree
  //
  pSubP->eSelector = kjLookup(pSubP->kjSubP, "entities");

  KjNode* notificationP = kjLookup(pSubP->kjSubP, "notification");
  pSubP->attrsSelector = (notificationP != NULL) ? kjLookup(notificationP, "attributes") : NULL;

  KjNode* langP = kjLookup(pSubP->kjSubP, "lang");
  pSubP->lang = (langP != NULL) ? langP->value.s : NULL;

  //
  // Update the @context for notifications - only if jsonldContext was explicitly present in the PATCH body.
  // The jsonldContext node in kjSubP is already updated by the merge loop above.
  // Here we just update the cached pSubP->context pointer (used for the Link header in notifications).
  //
  KjNode* jsonldContextInKjSubP = kjLookup(pSubP->kjSubP, "jsonldContext");
  if (jsonldContextInKjSubP != NULL)
    pSubP->context = jsonldContextInKjSubP->value.s;

  // Restore state (or new state if isActive was patched)
  pSubP->state = savedState;

  return true;
}
