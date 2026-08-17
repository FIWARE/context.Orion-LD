/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include <unistd.h>                                            // NULL

extern "C"
{
#include "kbase/kMacros.h"                                     // K_FT
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "common/sem.h"                                        // cacheSemTake, cacheSemGive

#include "orionld/types/QNode.h"                               // QNode, qNodeType
#include "orionld/types/OrionldAlteration.h"                   // OrionldAlteration, OrionldAlterationMatch, orionldAlterationType
#include "orionld/types/OrionldTenant.h"                       // OrionldTenant
#include "orionld/types/SubCache.h"                            // SubCache
#include "orionld/types/SubCacheItem.h"                        // SubCacheItem, SUB_TRIGGER
#include "orionld/types/SubEntitySelector.h"                   // SubEntitySelector
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/dotForEq.h"                           // dotForEq
#include "orionld/common/dateTime.h"                           // dateTimeFromString
#include "orionld/q/qPresent.h"                                // qPresent
#include "orionld/q/qMatch.h"                                  // qMatch
#include "orionld/subCache/subCacheItemStatusSet.h"            // subCacheItemStatusSet
#include "orionld/notifications/geoMatch.h"                    // geoMatch
#include "orionld/notifications/subCacheAlterationMatch.h"     // Own interface



// -----------------------------------------------------------------------------
//
// entitySelectorMatch - does the altered entity match the subscription's "entities"?
//
// One entity selector is an AND of its own members - "id" (or "idPattern") AND
// "type" - and a member that isn't there matches anything. The array of selectors
// is an OR: the entity matches the subscription if it matches ANY of them.
//
// There is no pattern for the Entity Type in NGSI-LD (unlike NGSIv2), but an
// NGSIv2 subscription may have "*" as its type, meaning "any type".
//
static bool entitySelectorMatch(SubCacheItem* sciP, const char* entityId, const char* entityType)
{
  for (SubEntitySelector* sesP = sciP->entitySelectors; sesP != NULL; sesP = sesP->next)
  {
    if (sesP->id != NULL)
    {
      if (strcmp(sesP->id, entityId) != 0)
        continue;
    }
    else if (sesP->idPattern != NULL)
    {
      if (sesP->idRegexP == NULL)                                   // The idPattern didn't compile - it matches nothing
        continue;

      if (regexec(sesP->idRegexP, entityId, 0, NULL, 0) != 0)
        continue;
    }

    if ((sesP->type != NULL) && ((sesP->type[0] != '*') || (sesP->type[1] != 0)))
    {
      if ((entityType == NULL) || (strcmp(sesP->type, entityType) != 0))
        continue;
    }

    return true;
  }

  KT_T(KtSubCacheMatch, "Sub '%s': no match due to the entity selectors (id: '%s', type: '%s')",
       sciP->subId,
       entityId,
       (entityType != NULL)? entityType : "no type");

  return false;
}



// -----------------------------------------------------------------------------
//
// matchLookup -
//
// Look into all OrionldAttributeAlteration of all matches for the same subscription
// if the entity ID and the alterationType coincide, then it's a match
//
// PARAMETERS
//   * matchP             an item in the 'matchList' - those already programmed for notification
//   * itemP              the candidate
//
static bool matchLookup(OrionldAlterationMatch* matchP, OrionldAlterationMatch* itemP)
{
#if 0
  // <DEBUG>
  KT_T(KtSubCacheMatch, "Match List:");
  for (OrionldAlterationMatch* mP = matchP; mP != NULL; mP = mP->next)
  {
    if (matchP->altAttrP)
      KT_T(KtSubCacheMatch, "o %p: %s %s", mP, mP->subP->subId, mP->altAttrP->alterationType);
    else
      KT_T(KtSubCacheMatch, "o %p: %s (no attr)", mP, mP->subP->subId);
  }
  KT_T(KtSubCacheMatch, "Compare with:");
  if (itemP->altAttrP)
    KT_T(KtSubCacheMatch, "o %p: %s %s", itemP, itemP->subP->subId, itemP->altAttrP->alterationType);
  else
    KT_T(KtSubCacheMatch, "o %p: %s (no attr)", itemP, itemP->subP->subId);
  // </DEBUG>
#endif

  // matchP is really the match-list. itemP is the one we're looking for
  while (matchP != NULL)
  {
    if (itemP->subP == matchP->subP)  // Same subscription - might be a match
    {
      if ((matchP->altAttrP == NULL) && (itemP->altAttrP == NULL))
      {
        //
        // If the altered entity is the same, this is a duplicate.
        // If the altered entity is different, then itemP's entity needs to be added to the datas array of matchP ...
        //
        if (strcmp(itemP->altP->entityId, matchP->altP->entityId) != 0)
          KT_W("Different entity (%s vs %s) - need to add it to the notification for sub %s", itemP->altP->entityId, matchP->altP->entityId, matchP->subP->subId);
        // return true;
      }
      else if ((matchP->altAttrP != NULL) && (itemP->altAttrP != NULL))
      {
        OrionldAlterationType inListAlterationType = matchP->altAttrP->alterationType;
        OrionldAlterationType candidate            = itemP->altAttrP->alterationType;

        if (inListAlterationType == candidate)
        {
          return true;
        }
      }
    }

    matchP = matchP->next;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// matchListInsert -
//
// NOTE:  This function puts matches in reverse order.
//        However, that is fixed later - the list is reversed back.
//
static OrionldAlterationMatch* matchListInsert(OrionldAlterationMatch* matchList, OrionldAlterationMatch* itemP)
{
  OrionldAlterationMatch* matchP = matchList;
  OrionldAlterationMatch* prev   = NULL;

  // Find the same subscription, to have all alteration-matches ordered
  while (matchP != NULL)
  {
    if (matchP->subP == itemP->subP)
    {
      //
      // insert it in the list, right BEFORE matchP (matchP == first occurrence of the subscription)
      //
      if (prev == NULL)
      {
        itemP->next = matchList;
        matchList   = itemP;
      }
      else
      {
        itemP->next = prev->next;  // prev->next === matchP
        prev->next = itemP;
      }

      return matchList;
    }

    prev   = matchP;
    matchP = matchP->next;
  }


  // First alteration-match of a subscription - prepending it to the matchList
  itemP->next = matchList;
  return itemP;  // As new matchList
}



// -----------------------------------------------------------------------------
//
// matchToMatchList -
//
static OrionldAlterationMatch* matchToMatchList
(
  OrionldAlterationMatch*      matchList,
  SubCacheItem*                subP,
  OrionldAlteration*           altP,
  OrionldAttributeAlteration*  aaP,
  int*                         matchesP
)
{
  OrionldAlterationMatch* amP = (OrionldAlterationMatch*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAlterationMatch));
  amP->altP     = altP;
  amP->altAttrP = aaP;
  amP->subP     = subP;

  if (matchList == NULL)
  {
    matchList  = amP;
    amP->next  = NULL;
    *matchesP += 1;
  }
  else
  {
    // Already there? - look up the existing subs in matchList to make sure we don't get any duplicates
    if (matchLookup(matchList, amP) == false)
    {
      matchList  = matchListInsert(matchList, amP);
      *matchesP += 1;
    }
  }

  return matchList;
}



// -----------------------------------------------------------------------------
//
// falseUpdate -
//
static bool falseUpdate(KjNode* attrP, KjNode* dbAttrsP)
{
  char eqAttrName[512];

  strncpy(eqAttrName, attrP->name, sizeof(eqAttrName) - 1);
  dotForEq(eqAttrName);

  KjNode* dbAttrP = kjLookup(dbAttrsP, eqAttrName);

  if (dbAttrP == NULL)  // New attribute - didn't exist
  {
    // KT_T(KtSubCacheMatch, "FU: NO  - NOT a False Update as '%s' did not exist before", attrP->name);
    return false;
  }

  KjNode* dbAttrValueP = kjLookup(dbAttrP, "value");

  if (dbAttrValueP == NULL)  // DB ERROR but ... never mind (will never happen)
  {
    // KT_T(KtSubCacheMatch, "FU: NO  - NOT a False Update as '%s' presents a DB error - no value field in the DB", attrP->name);
    return false;
  }

  KjNode* attrValueP = kjLookup(attrP, "value");

  if (attrValueP == NULL)  // No change in attribute value - false update
  {
    // KT_T(KtSubCacheMatch, "FU: YES - False Update as '%s' has no 'value' field in the normalized input", attrP->name);
    return true;
  }

  if (dbAttrValueP->type != attrValueP->type)  // Change in JSON type - real update
  {
    // KT_T(KtSubCacheMatch, "FU: NO  - NOT a False Update as '%s' the type of the attribute value is altered", attrP->name);
    return false;
  }

  if ((attrValueP->type == KjInt)     && (attrValueP->value.i != dbAttrValueP->value.i))    return false;
  if ((attrValueP->type == KjFloat)   && (attrValueP->value.f != dbAttrValueP->value.f))    return false;
  if ((attrValueP->type == KjBoolean) && (attrValueP->value.b != dbAttrValueP->value.b))    return false;

  // FIXME: Object + Array
  // KT_T(KtSubCacheMatch, "FU: PERHAPS - as Object + Array modification checks are still to be implemented (for '%s')", attrP->name);
  // KT_T(KtSubCacheMatch, "FU: YES - False Update as no value change was detected for '%s'", attrP->name);

  return true;
}



// -----------------------------------------------------------------------------
//
// watchedAttributeMatch - does a watchedAttributes entry match this changed attribute?
//
// A watchedAttributes entry may be datasetId-scoped using the syntax
// "attrName@datasetId" (split on the FIRST '@'): the subscription is then only
// triggered when an instance with that datasetId actually changed. The
// datasetId part is a URN compared verbatim; the token "@none" (i.e. an entry
// "attrName@@none") matches the default (no-datasetId) instance. Plain
// "attrName" entries (no '@') keep the original behaviour - match any instance -
// so existing subscriptions are completely unaffected.
//
// changedDatasetId is the datasetId of the changed instance (NULL = default/none,
// or unknown for a multi-instance Array patch).
//
static bool watchedAttributeMatch(const char* watched, const char* attrName, const char* changedDatasetId)
{
  const char* at = strchr(watched, '@');

  if (at == NULL)  // not datasetId-scoped - name-only match (original behaviour)
    return (strcmp(watched, attrName) == 0);

  size_t nameLen = at - watched;
  if ((strncmp(watched, attrName, nameLen) != 0) || (attrName[nameLen] != 0))
    return false;

  const char* wantedDs = at + 1;  // datasetId this subscription is scoped to

  if (strcmp(wantedDs, "@none") == 0)   // subscription wants the default (no-datasetId) instance
    return (changedDatasetId == NULL);

  if (changedDatasetId == NULL)         // a specific instance is wanted, but the default changed
    return false;

  return (strcmp(wantedDs, changedDatasetId) == 0);
}



// -----------------------------------------------------------------------------
//
// watchedListMatch - does ANY entry of "watchedAttributes" match this changed attribute?
//
static bool watchedListMatch(KjNode* watchedP, const char* attrName, const char* changedDatasetId)
{
  for (KjNode* wAttrP = watchedP->value.firstChildP; wAttrP != NULL; wAttrP = wAttrP->next)
  {
    KT_T(KtWatchedAttributes, "Comparing modified '%s' with watched '%s'", attrName, wAttrP->value.s);

    if (watchedAttributeMatch(wAttrP->value.s, attrName, changedDatasetId) == true)
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// attributeMatch -
//
static OrionldAlterationMatch* attributeMatch
(
  OrionldAlterationMatch*  matchList,
  SubCacheItem*            sciP,
  OrionldAlteration*       altP,
  int*                     matchesP
)
{
  int     matches  = 0;
  KjNode* watchedP = kjLookup(sciP->subTree, "watchedAttributes");
  bool    watched  = ((watchedP != NULL) && (watchedP->value.firstChildP != NULL));

  //
  // FIXME:
  //   No update should ever have ZERO alteredAttributes - I implemented that for convenience, but, can't stay.
  //   For now, this code inside "if (altP->alteredAttributes == 0)" stays but is all a bit "chapuza".
  //
  //   MIGHT BE I let "Creation" have zero alteredAttributes, as all attributes are created, none are removed nor updated ...
  //
  if (altP->alteredAttributes == 0)  // E.g. complete replace of an entity - treating it as EntityModified (for now)
  {
    KjNode* dbAttrsP = NULL;

    if ((altP->dbEntityP != NULL) && (noNotifyFalseUpdate == true))
      dbAttrsP = kjLookup(altP->dbEntityP, "attrs");

    //
    // watchedAttributes
    //
    bool match = (watched == false);  // If no watchedAttributes, then it's a match

    if ((watched == true) && (altP->inEntityP != NULL))
    {
      for (KjNode* attrP = altP->inEntityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        if (strcmp(attrP->name, "id")   == 0) continue;
        if (strcmp(attrP->name, "type") == 0) continue;

        KjNode*     dsP       = kjLookup(attrP, "datasetId");
        const char* changedDs = ((dsP != NULL) && (dsP->type == KjString))? dsP->value.s : NULL;

        if (watchedListMatch(watchedP, attrP->name, changedDs) == true)
        {
          if ((dbAttrsP == NULL) || (noNotifyFalseUpdate == false) || (falseUpdate(attrP, dbAttrsP) == false))
          {
            match = true;
            break;
          }
        }
      }
    }

    //
    // If no watchedAttributes - make sure not all attributes were unchanged (if noNotifyFalseUpdate in ON)
    // Only interesting of match == true
    // And of course, if the entity already existed (dbAttrsP != NULL)
    //
    if ((match == true) && (watched == false) && (dbAttrsP != NULL) && (noNotifyFalseUpdate == true) && (altP->inEntityP != NULL))
    {
      int changed = 0;

      for (KjNode* attrP = altP->inEntityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        if (strcmp(attrP->name, "id")   == 0) continue;
        if (strcmp(attrP->name, "type") == 0) continue;

        if (falseUpdate(attrP, dbAttrsP) == false)
          ++changed;
      }

      if (changed == 0)
        match = false;
    }

    // FIXME: Would need also to check those attributes that were deleted (either directly or via a REPLACE)

    // Is the Alteration type ON for this subscription?
    if (match == true)
    {
      if ((sciP->triggers & SUB_TRIGGER(EntityModified)) != 0)
        matchList = matchToMatchList(matchList, sciP, altP, NULL, &matches);
    }
    else
      KT_T(KtSubCacheMatch, "Sub '%s' - no match due to Watched Attributes", sciP->subId);
  }

  for (int aaIx = 0; aaIx < altP->alteredAttributes; aaIx++)
  {
    OrionldAttributeAlteration* aaP = &altP->alteredAttributeV[aaIx];

    if ((watched == true) && (watchedListMatch(watchedP, aaP->attrName, aaP->datasetId) == false))
    {
      KT_T(KtSubCacheMatch, "Sub '%s' - no match due to watchedAttributes", sciP->subId);
      continue;
    }

    // Is the Alteration type ON for this subscription?
    if ((sciP->triggers & SUB_TRIGGER(aaP->alterationType)) == 0)
    {
      KT_T(KtSubCacheMatch, "Sub '%s' - no match due to Trigger '%s'", sciP->subId, orionldAlterationType(aaP->alterationType));
      continue;
    }

    matchList = matchToMatchList(matchList, sciP, altP, aaP, &matches);
  }

  if (matches == 0)
    KT_T(KtSubCacheMatch, "Sub '%s' - no match due to Watched Attribute List (or Trigger!)", sciP->subId);
  else
    KT_T(KtSubCacheMatch, "Subscription '%s' is a MATCH", sciP->subId);
  *matchesP += matches;

  return matchList;
}



// -----------------------------------------------------------------------------
//
// subCacheAlterationMatch -
//
OrionldAlterationMatch* subCacheAlterationMatch(OrionldAlteration* alterationList, int* matchesP)
{
  OrionldAlterationMatch*  matchList = NULL;
  int                      matches   = 0;
  OrionldTenant*           tenantP   = orionldState.tenantP;
  SubCache*                scP       = tenantP->subCache;

  *matchesP = 0;

  if (scP == NULL)
    KT_RE(NULL, "No subscription cache for tenant '%s' - no subscription can match", tenantP->tenant);

  //
  // Loop over each alteration, and check ALL SUBSCRIPTIONS of the tenant for that alteration.
  // For each matching subscription, add the alterations into 'matchList'.
  //
  // The cache is per tenant, so there is no tenant to compare - a subscription that is
  // in there belongs to the tenant of the request, by construction.
  //
  cacheSemTake(__FUNCTION__, "Looping over sub-cache");

  for (OrionldAlteration* altP = alterationList; altP != NULL; altP = altP->next)
  {
    for (SubCacheItem* sciP = scP->subList; sciP != NULL; sciP = sciP->next)
    {
      //
      // 'isActive' is the one flag - it is false for a subscription that was
      // created inactive, that expired, or that was paused after too many
      // failed notifications. 'status' in the tree always says which one.
      //
      if (sciP->isActive == false)
      {
        KT_T(KtSubCacheMatch, "Sub '%s' - no match due to isActive == false", sciP->subId);
        continue;
      }

      if ((sciP->expiresAt > 0) && (sciP->expiresAt < orionldState.requestTime))
      {
        KT_T(KtSubCacheMatch, "Sub '%s' - no match due to expiration (now:%f, expired:%f)", sciP->subId, orionldState.requestTime, sciP->expiresAt);
        subCacheItemStatusSet(sciP, "expired");
        continue;
      }

      if ((sciP->throttling > 0) && ((orionldState.requestTime - sciP->lastNotificationTime) < sciP->throttling))
      {
        KT_T(KtSubCacheMatch, "Sub '%s' - no match due to throttling", sciP->subId);
        continue;
      }

      if ((sciP->entitySelectors != NULL) && (entitySelectorMatch(sciP, altP->entityId, altP->entityType) == false))
        continue;

      //
      // Check the "q" filter, BUT not if the verb is DELETE
      //
      if ((sciP->qP != NULL) && (orionldState.verb != HTTP_DELETE))
      {
        if (qMatch(sciP->qP, altP->finalApiEntityP, false) == false)
        {
          KT_T(KtSubCacheMatch, "Sub '%s' - no match due to ldq == '%s'", sciP->subId, sciP->qText);
          continue;
        }
      }

      //
      // Geo-match using GEOS (in-process, no DB query needed)
      //
      if (geoMatch(sciP, altP->finalApiEntityP) == false)
      {
        KT_T(KtSubCacheMatch, "Sub '%s' - no match due to geoQ", sciP->subId);
        continue;
      }

      matchList = attributeMatch(matchList, sciP, altP, &matches);  // Each call adds to matchList AND matches
    }
  }
  cacheSemGive(__FUNCTION__, "Looping over sub-cache");

  *matchesP = matches;

  return matchList;
}
