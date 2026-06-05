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
#include <stdlib.h>                                              // strdup, free
#include <string.h>                                              // strlen, strcpy, strcat
#include <time.h>                                               // time, gmtime_r, strftime

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjArray, kjString, kjChildAdd
#include "kjson/kjChildPrepend.h"                                // kjChildPrepend
}

#include "cache/subCache.h"                                      // CachedSubscription, subCacheItemLookup, subCacheItemRemove

#include "orionld/types/QNode.h"                                 // QNode
#include "orionld/types/OrionldRenderFormat.h"                   // OrionldRenderFormat, RF_NORMALIZED
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/common/uuidGenerate.h"                         // uuidGenerate
#include "orionld/common/subCacheApiSubscriptionInsert.h"        // subCacheApiSubscriptionInsert
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/payloadCheck/pCheckSubscription.h"             // pCheckSubscription
#include "orionld/dds/ddsActionSubscription.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionSubscriptionCreate -
//
char* ddsActionSubscriptionCreate
(
  const char*  entityId,
  const char*  entityType,
  const char*  attributeName,
  const char*  endpointUri,
  const char*  datasetId
)
{
  if (endpointUri == NULL)
    return NULL;

  //
  // Expand entity-type and attribute names so the temp subscription matches the
  // alterations emitted by the per-goal instance PATCHes (ddsActionSubAttributeUpdate
  // expands with orionldState.contextP too).
  //
  char* attrLongName = orionldAttributeExpand(orionldState.contextP, (char*) attributeName, true, NULL);
  char* typeLongName = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);

  // Subscription id - urn:ngsi-ld:subscription:<uuid>
  char subId[80];
  uuidGenerate(subId, sizeof(subId), "urn:ngsi-ld:subscription:");

  //
  // Build the API Subscription tree:
  //   { "type": "Subscription",
  //     "entities": [ { "id": <entityId>, "type": <typeLongName> } ],
  //     "watchedAttributes": [ <attrLongName> ],
  //     "notification": { "endpoint": { "uri": <endpointUri> } },
  //     "expiresAt": <now + 1h> }
  // The 'id' is kept separate (idNode) and prepended after pCheckSubscription,
  // exactly as orionldPostSubscriptions does.
  //
  KjNode* subP     = kjObject(orionldState.kjsonP, NULL);
  // NB: 'type' (and 'id') are passed to pCheckSubscription as separate nodes, NOT
  // added as children of subP - pCheckSubscription rejects them as "unknown fields"
  // if present in the tree (the normal POST flow extracts them out of the body too).
  KjNode* typeNode = kjString(orionldState.kjsonP, "type", "Subscription");

  KjNode* entitiesArr = kjArray(orionldState.kjsonP, "entities");
  KjNode* entityObj   = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(entityObj, kjString(orionldState.kjsonP, "id",   entityId));
  kjChildAdd(entityObj, kjString(orionldState.kjsonP, "type", typeLongName));
  kjChildAdd(entitiesArr, entityObj);
  kjChildAdd(subP, entitiesArr);

  //
  // watchedAttributes entry. When the goal has a datasetId we scope the TRIGGER
  // to it with the "attr@datasetId" syntax, so this temp subscription fires ONLY
  // when THIS goal's instance changes - not when a sibling goal that happens to
  // be in flight modifies the same attribute (avoids concurrent-goal cross-talk).
  // attrLongName is already expanded; pCheckSubscription's expansion of the name
  // part is idempotent.
  //
  char* watchedEntry = attrLongName;
  if (datasetId != NULL)
  {
    int len = strlen(attrLongName) + 1 /* '@' */ + strlen(datasetId) + 1 /* '\0' */;
    watchedEntry = kaAlloc(&orionldState.kalloc, len);
    strcpy(watchedEntry, attrLongName);
    strcat(watchedEntry, "@");
    strcat(watchedEntry, datasetId);
  }

  KjNode* watchedArr = kjArray(orionldState.kjsonP, "watchedAttributes");
  kjChildAdd(watchedArr, kjString(orionldState.kjsonP, NULL, watchedEntry));
  kjChildAdd(subP, watchedArr);

  // datasetId PROJECTION: notifications carry only THIS goal's instance (its
  // feedback/result/status), not the default instance or sibling goals. (The
  // @-suffix above scopes the trigger; this top-level datasetId scopes the body.)
  if (datasetId != NULL)
    kjChildAdd(subP, kjString(orionldState.kjsonP, "datasetId", datasetId));

  KjNode* notificationP = kjObject(orionldState.kjsonP, "notification");
  KjNode* endpointObj   = kjObject(orionldState.kjsonP, "endpoint");
  kjChildAdd(endpointObj, kjString(orionldState.kjsonP, "uri", endpointUri));
  kjChildAdd(notificationP, endpointObj);
  kjChildAdd(subP, notificationP);

  // expiresAt safety-net: now + 1h, so an action server that never sends a
  // terminal status can't leak the temp subscription forever.
  time_t    expiry = time(NULL) + 3600;
  struct tm tmStruct;
  char      expiresAt[40];
  gmtime_r(&expiry, &tmStruct);
  strftime(expiresAt, sizeof(expiresAt), "%Y-%m-%dT%H:%M:%SZ", &tmStruct);
  kjChildAdd(subP, kjString(orionldState.kjsonP, "expiresAt", expiresAt));

  KjNode* idNode = kjString(orionldState.kjsonP, "id", subId);

  //
  // Validate / normalise (expansion, endpoint, render format, ...).
  //
  KjNode*              endpointP       = NULL;
  KjNode*              ldqNodeP        = NULL;
  QNode*               qTree           = NULL;
  char*                qRenderedForDb  = NULL;
  bool                 qValidForV2     = false;
  bool                 qIsMq           = false;
  KjNode*              uriP            = NULL;
  KjNode*              notifierInfoP   = NULL;
  KjNode*              geoCoordinatesP = NULL;
  bool                 mqtt            = false;
  KjNode*              showChangesP    = NULL;
  KjNode*              sysAttrsP       = NULL;
  double               timeInterval    = 0;
  OrionldRenderFormat  renderFormat    = RF_NORMALIZED;

  bool ok = pCheckSubscription(subP,
                               true,
                               NULL,
                               idNode,
                               typeNode,
                               &endpointP,
                               &ldqNodeP,
                               &qTree,
                               &qRenderedForDb,
                               &qValidForV2,
                               &qIsMq,
                               &uriP,
                               &notifierInfoP,
                               &geoCoordinatesP,
                               &mqtt,
                               &showChangesP,
                               &sysAttrsP,
                               &timeInterval,
                               &renderFormat);
  if (ok == false)
  {
    KT_W("DDS action temp subscription failed pCheckSubscription (%s: %s) - no subscription created for endpoint '%s'",
         orionldState.pd.title, orionldState.pd.detail, endpointUri);
    return NULL;
  }

  // 'id' must be a child of the tree for the cache insert (mirrors orionldPostSubscriptions).
  kjChildPrepend(subP, idNode);

  //
  // Cache ONLY - the temp subscription is ephemeral (it is torn down on the
  // goal's terminal status, and goals themselves are in-memory), so we
  // deliberately skip mongocSubscriptionInsert.
  //
  CachedSubscription* cSubP = subCacheApiSubscriptionInsert(subP,
                                                            qTree,
                                                            geoCoordinatesP,
                                                            orionldState.contextP,
                                                            tenant0.tenant,
                                                            showChangesP,
                                                            sysAttrsP,
                                                            renderFormat);
  if (cSubP == NULL)
  {
    KT_W("DDS action temp subscription cache insert failed for endpoint '%s'", endpointUri);
    return NULL;
  }

  KT_T(StDdsAction, "Created temp action subscription '%s' on attr '%s' -> '%s'", subId, attrLongName, endpointUri);

  return strdup(subId);
}



// -----------------------------------------------------------------------------
//
// ddsActionSubscriptionDelete -
//
void ddsActionSubscriptionDelete(const char* subscriptionId)
{
  if (subscriptionId == NULL)
    return;

  CachedSubscription* cSubP = subCacheItemLookup(tenant0.tenant, subscriptionId);
  if (cSubP == NULL)
  {
    KT_T(StDdsAction, "Temp action subscription '%s' not in cache (already gone?)", subscriptionId);
    return;
  }

  subCacheItemRemove(cSubP);
  KT_T(StDdsAction, "Removed temp action subscription '%s'", subscriptionId);
}
