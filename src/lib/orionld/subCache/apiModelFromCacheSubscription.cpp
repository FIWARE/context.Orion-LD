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
#include <string.h>                                              // strchr, strlen, strcpy, strcat

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjString, kjInteger, kjBoolean, kjChildAdd, kjChildRemove
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "common/RenderFormat.h"                                 // renderFormatToString
#include "common/MimeType.h"                                     // mimeTypeToLongString

#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/orionldState.h"                         // orionldState, extras, experimental
#include "orionld/common/numberToDate.h"                         // numberToDate
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/dbModel/dbModelValueStrip.h"                   // dbModelValueStrip
#include "orionld/q/qAliasCompact.h"                             // qAliasCompact
#include "orionld/subCache/apiModelFromCacheSubscription.h"      // Own interface



// -----------------------------------------------------------------------------
//
// The order the members of a Subscription are rendered in.
//
// The cached tree keeps whatever order the write path gave it - the API response
// gets a fixed one. Anything not in the list (there shouldn't be anything) keeps
// its relative position, at the end.
//
static const char* subMemberOrderV[] =
{
  "id",
  "type",
  "subscriptionName",
  "description",
  "entities",
  "watchedAttributes",
  "datasetId",
  "q",
  "geoQ",
  "status",
  "isActive",
  "notification",
  "expiresAt",
  "throttling",
  "lang",
  "createdAt",
  "modifiedAt",
  "subordinate",
  "origin",
  "jsonldContext"
};

static const char* notificationMemberOrderV[] =
{
  "attributes",
  "format",
  "showChanges",
  "sysAttrs",
  "endpoint",
  "status",
  "timesSent",
  "timesFailed",
  "lastNotification",
  "lastFailure",
  "lastSuccess",
  "consecutiveErrors",
  "lastErrorReason"
};

static const char* endpointMemberOrderV[] = { "uri", "accept", "receiverInfo", "notifierInfo" };
static const char* entityMemberOrderV[]   = { "id", "idPattern", "type" };
static const char* geoqMemberOrderV[]     = { "geometry", "georel", "coordinates", "geoproperty" };



// -----------------------------------------------------------------------------
//
// memberOrder - reorder the members of an object, without allocating anything
//
static void memberOrder(KjNode* containerP, const char* orderV[], int orderItems)
{
  KjNode* headP = NULL;
  KjNode* tailP = NULL;

  for (int ix = 0; ix < orderItems; ix++)
  {
    KjNode* nodeP = kjLookup(containerP, orderV[ix]);

    if (nodeP == NULL)
      continue;

    kjChildRemove(containerP, nodeP);  // leaves nodeP->next as NULL

    if (tailP == NULL)
      headP = nodeP;
    else
      tailP->next = nodeP;

    tailP = nodeP;
  }

  if (headP == NULL)  // Not one known member - leave the container as it is
    return;

  tailP->next = containerP->value.firstChildP;  // Unknown members keep their order, at the end

  if (containerP->value.firstChildP == NULL)
    containerP->lastChild = tailP;

  containerP->value.firstChildP = headP;
}

#define MEMBER_ORDER(containerP, orderV) memberOrder(containerP, orderV, (int) (sizeof(orderV) / sizeof(orderV[0])))



// -----------------------------------------------------------------------------
//
// memberDrop -
//
static void memberDrop(KjNode* containerP, const char* name)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if (nodeP != NULL)
    kjChildRemove(containerP, nodeP);
}



// -----------------------------------------------------------------------------
//
// timestampSet - an absolute timestamp, straight from the cache item
//
// The cache item owns the notification timestamps (they change on every single
// notification, the tree only holds what the database holds), so what is in the
// tree is overwritten, or removed if there is nothing to show.
//
static void timestampSet(KjNode* containerP, const char* name, double timestamp)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if (timestamp <= 0.1)
  {
    if (nodeP != NULL)
      kjChildRemove(containerP, nodeP);
    return;
  }

  char dateTime[64];
  numberToDate(timestamp, dateTime, sizeof(dateTime));

  if (nodeP == NULL)
  {
    kjChildAdd(containerP, kjString(orionldState.kjsonP, name, dateTime));
    return;
  }

  nodeP->type    = KjString;
  nodeP->value.s = kaStrdup(&orionldState.kalloc, dateTime);
}



// -----------------------------------------------------------------------------
//
// counterSet - what the tree holds is what the database holds, plus the delta
//
// A counter is only part of a Subscription if it is greater than zero
// (TS 104-175, clause 5, NotificationParams).
//
static void counterSet(KjNode* containerP, const char* name, uint32_t delta)
{
  KjNode*  nodeP = kjLookup(containerP, name);
  int64_t  total = ((nodeP != NULL)? nodeP->value.i : 0) + delta;

  if (total <= 0)
  {
    if (nodeP != NULL)
      kjChildRemove(containerP, nodeP);
    return;
  }

  if (nodeP == NULL)
    kjChildAdd(containerP, kjInteger(orionldState.kjsonP, name, total));
  else
  {
    nodeP->type    = KjInt;
    nodeP->value.i = total;
  }
}



// -----------------------------------------------------------------------------
//
// stringSet - set (or create) a String member
//
static void stringSet(KjNode* containerP, const char* name, const char* value)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if (nodeP == NULL)
    kjChildAdd(containerP, kjString(orionldState.kjsonP, name, value));
  else
  {
    nodeP->type    = KjString;
    nodeP->value.s = (char*) value;
  }
}



// -----------------------------------------------------------------------------
//
// timestampToApi - a timestamp is a Number in the cache, an ISO8601 String in the API
//
static void timestampToApi(KjNode* containerP, const char* name, bool wanted)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if (nodeP == NULL)
    return;

  if (wanted == false)
  {
    kjChildRemove(containerP, nodeP);
    return;
  }

  if (nodeP->type == KjString)  // Already an ISO8601 String
    return;

  double timestamp = (nodeP->type == KjFloat)? nodeP->value.f : nodeP->value.i;

  if (timestamp <= 0)
  {
    kjChildRemove(containerP, nodeP);
    return;
  }

  char dateTime[64];
  numberToDate(timestamp, dateTime, sizeof(dateTime));

  nodeP->type    = KjString;
  nodeP->value.s = kaStrdup(&orionldState.kalloc, dateTime);
}



// -----------------------------------------------------------------------------
//
// aliasLookup - the alias of an attribute name, according to the request's @context
//
// A watchedAttributes entry may be datasetId-scoped ("attrName@datasetId") - only
// the name part is aliased, the datasetId is kept verbatim.
//
static char* aliasLookup(const char* longName)
{
  const char* atP = strchr(longName, '@');

  if (atP == NULL)
    return orionldContextItemAliasLookup(orionldState.contextP, (char*) longName, NULL, NULL);

  char* dup    = kaStrdup(&orionldState.kalloc, longName);
  char* dupAtP = strchr(dup, '@');

  *dupAtP = 0;

  char* nameAlias = orionldContextItemAliasLookup(orionldState.contextP, dup, NULL, NULL);
  int   len       = strlen(nameAlias) + strlen(atP) + 1;   // atP starts at the '@'
  char* alias     = kaAlloc(&orionldState.kalloc, len);

  strcpy(alias, nameAlias);
  strcat(alias, atP);

  return alias;
}



// -----------------------------------------------------------------------------
//
// stringArrayAlias - alias every item of an Array of attribute names
//
static void stringArrayAlias(KjNode* arrayP)
{
  if (arrayP == NULL)
    return;

  for (KjNode* itemP = arrayP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type == KjString)
      itemP->value.s = aliasLookup(itemP->value.s);
  }
}



// -----------------------------------------------------------------------------
//
// notificationToApi -
//
static void notificationToApi(KjNode* notificationP, SubCacheItem* sciP)
{
  stringArrayAlias(kjLookup(notificationP, "attributes"));

  //
  // 'format' and 'endpoint::accept' are always part of the response - the compiled
  // state has them, with their default already applied, and in the ONE spelling
  // the API uses (a request may have said "keyValues" or "simplified").
  //
  stringSet(notificationP, "format", renderFormatToString(sciP->renderFormat));

  KjNode* endpointP = kjLookup(notificationP, "endpoint");
  if (endpointP != NULL)
  {
    stringSet(endpointP, "accept", mimeTypeToLongString(sciP->mimeType));
    MEMBER_ORDER(endpointP, endpointMemberOrderV);
  }

  //
  // The notification status, the counters and the timestamps all belong to the
  // cache item - the tree only holds what has been flushed to the database.
  //
  stringSet(notificationP, "status", (sciP->consecutiveErrors == 0)? "ok" : "failed");

  counterSet(notificationP, "timesSent",   sciP->deltas.timesSent);
  counterSet(notificationP, "timesFailed", sciP->deltas.timesFailed);

  timestampSet(notificationP, "lastNotification", sciP->lastNotificationTime);
  timestampSet(notificationP, "lastSuccess",      sciP->lastSuccess);
  timestampSet(notificationP, "lastFailure",      sciP->lastFailure);

  memberDrop(notificationP, "consecutiveErrors");
  if (sciP->consecutiveErrors > 0)
    kjChildAdd(notificationP, kjInteger(orionldState.kjsonP, "consecutiveErrors", sciP->consecutiveErrors));

  memberDrop(notificationP, "lastErrorReason");
  if (sciP->lastErrorReason[0] != 0)
    kjChildAdd(notificationP, kjString(orionldState.kjsonP, "lastErrorReason", sciP->lastErrorReason));

  MEMBER_ORDER(notificationP, notificationMemberOrderV);
}



// -----------------------------------------------------------------------------
//
// apiModelFromCacheSubscription -
//
// The cached subTree IS the API model - what is left to do is to say it in the
// terms of THIS request: the aliases of its @context, ISO8601 for the timestamps,
// and the members that only ever exist in a response.
//
void apiModelFromCacheSubscription(KjNode* apiSubP, SubCacheItem* sciP, bool sysAttrs, bool contextInBody)
{
  memberDrop(apiSubP, "hostAlias");

  //
  // "q" and "mq" are the NGSIv2 renderings of 'q'. The cache keeps them - the
  // NGSIv2 StringFilters are compiled from them - but they are not part of an
  // NGSI-LD Subscription, so they do not go out in the response. The NGSI-LD 'q'
  // is "ldQ", handled below.
  //
  memberDrop(apiSubP, "q");
  memberDrop(apiSubP, "mq");

  //
  // entities - the entity type is stored expanded
  //
  KjNode* entitiesP = kjLookup(apiSubP, "entities");
  if (entitiesP != NULL)
  {
    for (KjNode* entityP = entitiesP->value.firstChildP; entityP != NULL; entityP = entityP->next)
    {
      KjNode* typeP = kjLookup(entityP, "type");

      if (typeP != NULL)
        typeP->value.s = orionldContextItemAliasLookup(orionldState.contextP, typeP->value.s, NULL, NULL);

      MEMBER_ORDER(entityP, entityMemberOrderV);
    }
  }

  stringArrayAlias(kjLookup(apiSubP, "watchedAttributes"));

  //
  // 'q' - stored expanded and in the database's value notation, under the name "ldQ"
  //
  KjNode* qP = kjLookup(apiSubP, "ldQ");
  if (qP != NULL)
  {
    qP->name = (char*) "q";
    dbModelValueStrip(qP);
    qAliasCompact(qP, true);  // qAliasCompact uses orionldState.contextP
  }

  //
  // geoQ - the geoproperty is stored expanded, and dot-for-eq encoded
  //
  KjNode* geoqP = kjLookup(apiSubP, "geoQ");
  if (geoqP != NULL)
  {
    KjNode* geopropertyP = kjLookup(geoqP, "geoproperty");

    if (geopropertyP != NULL)
    {
      char* dotName = kaStrdup(&orionldState.kalloc, geopropertyP->value.s);

      eqForDot(dotName);
      geopropertyP->value.s = orionldContextItemAliasLookup(orionldState.contextP, dotName, NULL, NULL);
    }

    MEMBER_ORDER(geoqP, geoqMemberOrderV);
  }

  //
  // 'isActive' is "true by default" (TS 104-175, clause 5) and a subscription
  // created without it has it nowhere but in the compiled state
  //
  if (kjLookup(apiSubP, "isActive") == NULL)
    kjChildAdd(apiSubP, kjBoolean(orionldState.kjsonP, "isActive", sciP->isActive));

  KjNode* notificationP = kjLookup(apiSubP, "notification");
  if (notificationP != NULL)
    notificationToApi(notificationP, sciP);
  else
    KT_E("Cached subscription '%s' without a 'notification' member", sciP->subId);

  timestampToApi(apiSubP, "expiresAt",  true);
  timestampToApi(apiSubP, "createdAt",  sysAttrs);
  timestampToApi(apiSubP, "modifiedAt", sysAttrs);

  if ((experimental == true) && (extras == true))
    kjChildAdd(apiSubP, kjString(orionldState.kjsonP, "origin", "cache"));

  MEMBER_ORDER(apiSubP, subMemberOrderV);

  //
  // The @context goes last - it is not part of the Subscription
  //
  if (contextInBody == true)
    kjChildAdd(apiSubP, kjString(orionldState.kjsonP, "@context", orionldState.contextP->url));
}
