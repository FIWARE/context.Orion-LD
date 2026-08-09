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
#include <stddef.h>                                              // NULL
#include <string.h>                                              // strcmp

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjFloat, kjString, kjChildAdd, kjChildRemove
#include "kjson/kjFree.h"                                        // kjFree
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/common/dateTime.h"                             // dateTimeFromString
#include "orionld/subCache/apiModelToCacheSubscription.h"        // Own interface



// -----------------------------------------------------------------------------
//
// memberDrop - remove a member from the cached tree - and free it
//
static void memberDrop(KjNode* containerP, const char* name)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if (nodeP == NULL)
    return;

  kjChildRemove(containerP, nodeP);
  kjFree(nodeP);
}



// -----------------------------------------------------------------------------
//
// memberRename - the API accepts two names for the same thing, the cache holds one
//
static void memberRename(KjNode* containerP, const char* from, const char* to)
{
  KjNode* nodeP = kjLookup(containerP, from);

  if (nodeP != NULL)
    nodeP->name = (char*) to;
}



// -----------------------------------------------------------------------------
//
// timestampToFloat - a timestamp is a Number in the cache, whoever put it there
//
// The database hands over a Number, an API request an ISO8601 String. The node is
// REPLACED, not modified in place - a KjString owns its value and kjFree is what
// gives it back.
//
static void timestampToFloat(KjNode* containerP, const char* name)
{
  KjNode* nodeP = kjLookup(containerP, name);

  if ((nodeP == NULL) || (nodeP->type != KjString))
    return;

  char   errorString[256];
  double timestamp = dateTimeFromString(nodeP->value.s, errorString, sizeof(errorString));

  if (timestamp < 0)  // Not a valid ISO8601 - leave it, subCacheItemCompile parses Strings as well
    return;

  kjChildRemove(containerP, nodeP);
  kjFree(nodeP);

  kjChildAdd(containerP, kjFloat(NULL, name, timestamp));
}



// -----------------------------------------------------------------------------
//
// apiModelToCacheSubscription -
//
// Brings an API-model Subscription into the ONE shape the cache holds, in place,
// on the cache item's own (malloc'ed) clone of the tree.
//
// Three write paths feed the cache and they do not agree on the details: a POST
// hands over the request tree (an 'expiresAt' as an ISO8601 String, the NGSIv2
// "q"/"mq" renderings that only the database wants, and it accepts 'expires' and
// 'name' as aliases), while the startup/PATCH path hands over what
// dbModelToApiSubscription makes of a database subscription (Numbers, a "tenant").
// Everything downstream - the matcher, the notification send path, GET - reads
// the subTree, so the shape has to be settled HERE, once, and not guessed at by
// every reader.
//
// TREE SHAPE ONLY: the compiled matching state ('q' as a QNode tree, the GEOS
// geometry, the idPattern regexes, the split endpoint, ...) is NOT built here -
// it is built by subCacheItemCompile, which runs right after.
//
// NOTE
//   Member ORDER is not settled here - it is the API rendering that needs a fixed
//   order, and apiModelFromCacheSubscription gives it one after having added the
//   members that only exist in the response.
//
void apiModelToCacheSubscription(KjNode* apiSubscriptionP, OrionldContext* jsonldContextP)
{
  //
  // 'type' and 'jsonldContext' are part of a Subscription but not of a POST
  // payload: the type is pulled out of the request tree by the payload check,
  // and the @context of the creating request is the subscription's context -
  // that is what goes to the database (as "ldContext"), so it goes here too.
  //
  if (kjLookup(apiSubscriptionP, "type") == NULL)
    kjChildAdd(apiSubscriptionP, kjString(NULL, "type", "Subscription"));

  if ((jsonldContextP != NULL) && (kjLookup(apiSubscriptionP, "jsonldContext") == NULL))
    kjChildAdd(apiSubscriptionP, kjString(NULL, "jsonldContext", jsonldContextP->url));

  //
  // The two accepted aliases. Not both - pCheckSubscription treats the pair as a
  // duplicated member and responds 400.
  //
  memberRename(apiSubscriptionP, "expires", "expiresAt");
  memberRename(apiSubscriptionP, "name",    "subscriptionName");

  //
  // "q" and "mq" are the NGSIv2 renderings of the very same filter, and they STAY:
  // subCacheItemV2Compile builds the NGSIv2 StringFilters out of them, which is
  // what lets an entity updated through the NGSIv2 API match this subscription.
  // They are not part of the API model, so the rendering drops them instead
  // (apiModelFromCacheSubscription).
  //
  // "tenant" is meaningless in a per-tenant cache, and "origin" belongs to a
  // response, not to a subscription.
  //
  memberDrop(apiSubscriptionP, "tenant");
  memberDrop(apiSubscriptionP, "origin");

  timestampToFloat(apiSubscriptionP, "expiresAt");
  timestampToFloat(apiSubscriptionP, "createdAt");
  timestampToFloat(apiSubscriptionP, "modifiedAt");

  //
  // 'datasetId' is a String or an Array of String on input - an Array in the cache
  //
  KjNode* datasetIdP = kjLookup(apiSubscriptionP, "datasetId");
  if ((datasetIdP != NULL) && (datasetIdP->type == KjString))
  {
    KjNode* arrayP = kjArray(NULL, "datasetId");

    kjChildRemove(apiSubscriptionP, datasetIdP);
    kjChildAdd(arrayP, datasetIdP);
    kjChildAdd(apiSubscriptionP, arrayP);
  }

  //
  // An entity selector with an 'idPattern' of ".*" selects every entity id -
  // which is what a selector without an id does. The database model cannot tell
  // the two apart (it stores "id": ".*", "isPattern": true for both), so neither
  // does the cache.
  //
  KjNode* entitiesP = kjLookup(apiSubscriptionP, "entities");
  if (entitiesP != NULL)
  {
    for (KjNode* entityP = entitiesP->value.firstChildP; entityP != NULL; entityP = entityP->next)
    {
      KjNode* idPatternP = kjLookup(entityP, "idPattern");

      if ((idPatternP != NULL) && (idPatternP->type == KjString) && (strcmp(idPatternP->value.s, ".*") == 0))
        memberDrop(entityP, "idPattern");
    }
  }

  //
  // 'showChanges' and 'sysAttrs' are "false by default" and the database only
  // ever holds them when true - so a request that spelled the default out does
  // not make the cached subscription look different from the stored one.
  //
  KjNode* notificationP = kjLookup(apiSubscriptionP, "notification");
  if (notificationP != NULL)
  {
    KjNode* showChangesP = kjLookup(notificationP, "showChanges");
    KjNode* sysAttrsP    = kjLookup(notificationP, "sysAttrs");

    if ((showChangesP != NULL) && (showChangesP->type == KjBoolean) && (showChangesP->value.b == false))
      memberDrop(notificationP, "showChanges");

    if ((sysAttrsP != NULL) && (sysAttrsP->type == KjBoolean) && (sysAttrsP->value.b == false))
      memberDrop(notificationP, "sysAttrs");
  }

  //
  // 'runNo' is how the POST path keeps track of the subordinate subscriptions it
  // has created - it is not part of the Subscription
  //
  KjNode* subordinateP = kjLookup(apiSubscriptionP, "subordinate");
  if (subordinateP != NULL)
  {
    for (KjNode* subSubP = subordinateP->value.firstChildP; subSubP != NULL; subSubP = subSubP->next)
    {
      memberDrop(subSubP, "runNo");
    }
  }
}
