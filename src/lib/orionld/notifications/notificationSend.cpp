/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include <string.h>                                              // strlen
#include <sys/uio.h>                                             // writev, iovec
#include <sys/select.h>                                          // select
#include <curl/curl.h>                                           // curl

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "ktrace/ktTraceLevelCheck.h"                            // ktTraceLevelCheck
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjBuilder.h"                                     // kjObject, kjArray, kjString, kjChildAdd, ...
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjClone.h"                                       // kjClone
}

#include "orionld/types/OrionldAlteration.h"                     // OrionldAlterationMatch, OrionldAlteration, orionldAlterationType
#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/orionldState.h"                         // orionldState, coreContextUrl, userAgentHeader
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/numberToDate.h"                         // numberToDate
#include "orionld/common/uuidGenerate.h"                         // uuidGenerate
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/common/langStringExtract.h"                    // langStringExtract
#include "orionld/kjTree/kjEntityIdLookupInEntityArray.h"        // kjEntityIdLookupInEntityArray
#include "orionld/context/orionldCoreContext.h"                  // orionldCoreContextP
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/mqtt/mqttNotify.h"                             // mqttNotify
#include "orionld/ws/wsNotify.h"                                 // wsNotify
#include "orionld/notifications/httpNotify.h"                    // httpNotify
#include "orionld/notifications/httpsNotify.h"                   // httpsNotify
#include "orionld/notifications/notificationDataToGeoJson.h"     // notificationDataToGeoJson
#include "orionld/notifications/previousValueAdd.h"              // previousValueAdd
#include "orionld/notifications/notificationSend.h"              // Own interface



// -----------------------------------------------------------------------------
//
// Fixed value headers - mocve to separate file - also used in pernot/pernotSend.cpp
//
const char* contentTypeHeaderJson    = (char*) "Content-Type: application/json\r\n";
const char* contentTypeHeaderJsonLd  = (char*) "Content-Type: application/ld+json\r\n";
const char* contentTypeHeaderGeoJson = (char*) "Content-Type: application/geo+json\r\n";
const char* acceptHeader             = (char*) "Accept: application/json\r\n";

const char* normalizedHeader         = (char*) "Ngsild-Attribute-Format: Normalized\r\n";
const char* conciseHeader            = (char*) "Ngsild-Attribute-Format: Concise\r\n";
const char* simplifiedHeader         = (char*) "Ngsild-Attribute-Format: Simplified\r\n";

const char* normalizedHeaderNgsiV2   = (char*) "Ngsiv2-Attrsformat: normalized\r\n";
const char* keyValuesHeaderNgsiV2    = (char*) "Ngsiv2-Attrsformat: keyValues\r\n";

char    userAgentHeader[64];     // "User-Agent: orionld/" + ORIONLD_VERSION + \r\n" - initialized in orionldServiceInit()
size_t  userAgentHeaderLen = 0;  // Set in orionldServiceInit()



// -----------------------------------------------------------------------------
//
// subNotificationMember - a member of the Subscription's "notification" object
//
// The subscription tree is the source of truth for everything the notification
// path needs but the matcher doesn't - the notified attributes, the datasetId
// filter, receiverInfo, notifierInfo. Only NON-EMPTY arrays are handed back:
// "no such member" and "an empty array" mean the same thing to every caller.
//
static KjNode* subNotificationMember(SubCacheItem* sciP, const char* member)
{
  KjNode* notificationP = kjLookup(sciP->subTree, "notification");

  if (notificationP == NULL)
    return NULL;

  KjNode* memberP = kjLookup(notificationP, member);

  if ((memberP != NULL) && (memberP->type == KjArray) && (memberP->value.firstChildP == NULL))
    return NULL;

  return memberP;
}



// -----------------------------------------------------------------------------
//
// subEndpointMember - a member of "notification::endpoint" (receiverInfo, notifierInfo)
//
static KjNode* subEndpointMember(SubCacheItem* sciP, const char* member)
{
  KjNode* endpointP = subNotificationMember(sciP, "endpoint");

  if (endpointP == NULL)
    return NULL;

  KjNode* memberP = kjLookup(endpointP, member);

  if ((memberP != NULL) && (memberP->value.firstChildP == NULL))
    return NULL;

  return memberP;
}



// -----------------------------------------------------------------------------
//
// keyValueLookup - the value of a { "key": ..., "value": ... } item in an array
//
static char* keyValueLookup(KjNode* arrayP, const char* key)
{
  if (arrayP == NULL)
    return NULL;

  for (KjNode* itemP = arrayP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    KjNode* keyP   = kjLookup(itemP, "key");
    KjNode* valueP = kjLookup(itemP, "value");

    if ((keyP != NULL) && (valueP != NULL) && (valueP->type == KjString) && (strcmp(keyP->value.s, key) == 0))
      return valueP->value.s;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// attributeToSimplified - move to its own module
//
// 1. Find the type
// 2. Knowing the type, find the value ("value", "object", or "languageMap")
// 3. Make the value node the RHS of the attribute
//
static void attributeToSimplified(KjNode* attrP, const char* lang)
{
  bool languangeMap = false;

  // Get the attribute type
  KjNode* attrTypeP = kjLookup(attrP, "type");
  if (attrTypeP       == NULL)      KT_RVE("Attribute '%s' has no type", attrP->name);
  if (attrTypeP->type != KjString)  KT_RVE("Attribute '%s' has a type that is not a JSON String", attrP->name);

  // Get the value
  char* valueFieldName = (char*) "value";
  if (strcmp(attrTypeP->value.s, "Relationship") == 0)
    valueFieldName = (char*) "object";
  else if (strcmp(attrTypeP->value.s, "LanguageProperty") == 0)
  {
    languangeMap = true;
    valueFieldName = (char*) "languageMap";
  }

  KjNode* valueP = kjLookup(attrP, valueFieldName);

  if (valueP == NULL)
    KT_RVE("Attribute '%s' has no value '%s'", attrP->name, valueFieldName);

  if ((languangeMap == true) && (lang[0] != 0))
  {
    char*   pickedLanguage;  // Not used (Simplified), but langStringExtract needs it
    KjNode* langNodeP = langItemPick(valueP, attrP->name, lang, &pickedLanguage);

    attrP->value       = langNodeP->value;
    attrP->type        = langNodeP->type;
  }
  else
  {
    attrP->type  = valueP->type;
    attrP->value = valueP->value;
  }
}



// -----------------------------------------------------------------------------
//
// attributeToConcise - move to its own module
//
// 1. Find and remove the type
// 2. If only one item left and it's "value" - Simplified
//
static void attributeToConcise(KjNode* attrP, bool* simplifiedP, const char* lang)
{
  // Get the attribute type and remove it
  KjNode* attrTypeP = kjLookup(attrP, "type");
  if (attrTypeP == NULL)
    KT_RVE("Attribute '%s' has no type", attrP->name);
  if (attrTypeP->type != KjString)
    KT_RVE("Attribute '%s' has a type that is not a JSON String", attrP->name);

  kjChildRemove(attrP, attrTypeP);

  if ((lang[0] != 0) && (strcmp(attrTypeP->value.s, "LanguageProperty") == 0))
  {
    KjNode* valueP    = kjLookup(attrP, "languageMap");
    char*   pickedLanguage;  // Name of the picked language
    KjNode* langNodeP = langItemPick(valueP, attrP->name, lang, &pickedLanguage);

    valueP->value       = langNodeP->value;
    valueP->type        = langNodeP->type;
    valueP->name        = (char*) "value";

    KjNode* langP   = kjString(orionldState.kjsonP, "lang", pickedLanguage);
    kjChildAdd(attrP, langP);
    return;
  }

  if ((strcmp(attrTypeP->value.s, "Property") != 0) && (strcmp(attrTypeP->value.s, "GeoProperty") != 0))
    return;

  // If only one item left in attrP - Simplified
  if ((attrP->value.firstChildP != NULL) && (attrP->value.firstChildP->next == NULL))
  {
    attrP->type  = attrP->value.firstChildP->type;
    attrP->value = attrP->value.firstChildP->value;
    *simplifiedP = true;
  }
}



// -----------------------------------------------------------------------------
//
// attributeToNormalized -
//
static void attributeToNormalized(KjNode* attrP, const char* lang)
{
  KjNode* attrTypeP = kjLookup(attrP, "type");

  //
  // LanguageProperty and 'lang'
  //
  if (attrTypeP != NULL)
  {
    if ((lang[0] != 0) && (strcmp(attrTypeP->value.s, "LanguageProperty")  == 0))
    {
      KjNode* valueP = kjLookup(attrP, "languageMap");
      if (valueP != NULL)
      {
        char*   pickedLanguage;  // Name of the picked language
        KjNode* langNodeP = langItemPick(valueP, attrP->name, lang, &pickedLanguage);

        valueP->value       = langNodeP->value;
        valueP->type        = langNodeP->type;

        // Change type form 'LanguageProperty' to 'Property'
        attrTypeP->value.s = (char*) "Property";

        // Change "languageMap" to "value" for the value
        valueP->name = (char*) "value";

        KjNode* langP = kjString(orionldState.kjsonP, "lang", pickedLanguage);
        kjChildAdd(attrP, langP);
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// attributeFix - compaction and format (concise, simplified, normalized)
//
static void attributeFix(KjNode* attrP, SubCacheItem* subP)
{
  bool        simplified = (subP->renderFormat == RF_SIMPLIFIED);
  bool        concise    = (subP->renderFormat == RF_CONCISE);
  const char* lang       = (subP->lang != NULL)? subP->lang : "";

  // Never mind "location", "observationSpace", and "operationSpace"
  // It is probably faster to lookup their alias (and get the same back) as it is to
  // do three string-comparisons in every loop
  //
  eqForDot(attrP->name);
  char* attrLongName = attrP->name;

  attrP->name = orionldContextItemAliasLookup(subP->contextP, attrP->name, NULL, NULL);

  //
  // ".added" and ".removed" are help arrays for Merge+Patch behaviour
  // They shouldn't be here at this point but if they are, they're to be ignored
  // So, we just remove them, for now.
  //
  // All this needs to be carefully studied and probably modified.
  //
  KjNode* addedP    = kjLookup(attrP, ".added");
  KjNode* removedP  = kjLookup(attrP, ".removed");

  if (addedP   != NULL) kjChildRemove(attrP, addedP);
  if (removedP != NULL) kjChildRemove(attrP, removedP);


  //
  // If vocab-property, its value needs to be compacted
  //
  KjNode* vocabP = kjLookup(attrP, "vocab");
  if (vocabP != NULL)
  {
    if (vocabP->type == KjString)
      vocabP->value.s = orionldContextItemAliasLookup(subP->contextP, vocabP->value.s, NULL, NULL);
    else if (vocabP->type == KjArray)
    {
      for (KjNode* wordP = vocabP->value.firstChildP; wordP != NULL; wordP = wordP->next)
      {
        if (wordP->type == KjString)
          wordP->value.s = orionldContextItemAliasLookup(subP->contextP, wordP->value.s, NULL, NULL);
      }
    }
  }

  bool asSimplified = false;
  if (attrP->type == KjObject)
  {
    if      (simplified)  attributeToSimplified(attrP, lang);
    else if (concise)     attributeToConcise(attrP, &asSimplified, lang);
    else                  attributeToNormalized(attrP, lang);
  }

  if ((asSimplified == false) && (simplified == false))
  {
    //
    // Here we're "in subAttributeFix"
    //

    // Add the "previousValue", unless RF_SIMPLIFIED
    if ((subP->renderFormat != RF_SIMPLIFIED) && (subP->showChanges == true))
      previousValueAdd(attrP, attrLongName);

    for (KjNode* saP = attrP->value.firstChildP; saP != NULL; saP = saP->next)
    {
      if (strcmp(saP->name, "type")        == 0) continue;
      if (strcmp(saP->name, "value")       == 0) continue;
      if (strcmp(saP->name, "object")      == 0) continue;
      if (strcmp(saP->name, "languageMap") == 0) continue;
      if (strcmp(saP->name, "vocab")       == 0) continue;
      if (strcmp(saP->name, "unitCode")    == 0) continue;
      if (strcmp(saP->name, "createdAt")   == 0) continue;
      if (strcmp(saP->name, "modifiedAt")  == 0) continue;

      eqForDot(saP->name);
      saP->name = orionldContextItemAliasLookup(subP->contextP, saP->name, NULL, NULL);

      if (saP->type == KjObject)
      {
        if (subP->renderFormat == RF_SIMPLIFIED)
          attributeToSimplified(saP, lang);
        else if (subP->renderFormat == RF_CONCISE)
          attributeToConcise(saP, &asSimplified, lang);  // asSimplified is not used down here

        // Sub-sub-attrs (only if still an object after simplification/concise)
        if (saP->type != KjObject)
          continue;

        for (KjNode* ssaP = saP->value.firstChildP; ssaP != NULL; ssaP = ssaP->next)
        {
          if (strcmp(ssaP->name, "type")        == 0) continue;
          if (strcmp(ssaP->name, "value")       == 0) continue;
          if (strcmp(ssaP->name, "object")      == 0) continue;
          if (strcmp(ssaP->name, "languageMap") == 0) continue;
          if (strcmp(ssaP->name, "vocab")       == 0) continue;
          if (strcmp(ssaP->name, "createdAt")   == 0) continue;
          if (strcmp(ssaP->name, "modifiedAt")  == 0) continue;

          eqForDot(ssaP->name);
          ssaP->name = orionldContextItemAliasLookup(subP->contextP, ssaP->name, NULL, NULL);

          if (ssaP->type == KjObject)
          {
            if (subP->renderFormat == RF_SIMPLIFIED)
              attributeToSimplified(ssaP, lang);
            else if (subP->renderFormat == RF_CONCISE)
              attributeToConcise(ssaP, &asSimplified, lang);  // asSimplified is not used down here
          }
        }
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// entityFix - compaction and format (concise, simplified, normalized)
//
KjNode* entityFix(KjNode* originalEntityP, SubCacheItem* subP)
{
  KjNode* entityP   = kjClone(orionldState.kjsonP, originalEntityP);

  //
  // ".added" and ".removed" are help arrays for Merge+Patch behaviour
  // They shouldn't be here at this point but if they are, they're to be ignored
  // So, we just remove them, for now.
  //
  // All this needs to be carefully studied and probably modified.
  //
  KjNode* addedP    = kjLookup(entityP, ".added");
  KjNode* removedP  = kjLookup(entityP, ".removed");

  if (addedP   != NULL) kjChildRemove(entityP, addedP);
  if (removedP != NULL) kjChildRemove(entityP, removedP);

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (strcmp(attrP->name, "id")         == 0) continue;
    if (strcmp(attrP->name, "createdAt")  == 0) continue;
    if (strcmp(attrP->name, "modifiedAt") == 0) continue;
    if (strcmp(attrP->name, "deletedAt")  == 0) continue;
    if (strcmp(attrP->name, "observedAt") == 0) continue;

    if (strcmp(attrP->name, "type") == 0)
    {
      attrP->value.s = orionldContextItemAliasLookup(subP->contextP, attrP->value.s, NULL, NULL);
      continue;
    }

    attributeFix(attrP, subP);  // FIXME: No need to call this function for DELETE Op ...
  }

  return entityP;
}



// -----------------------------------------------------------------------------
//
// orionldEntityToNgsiV2 -
//
KjNode* orionldEntityToNgsiV2(OrionldContext* contextP, KjNode* entityP, bool keyValues, bool compact)
{
  KjNode* v2EntityP = kjClone(orionldState.kjsonP, entityP);

  // For all attributes, create a "metadata" object and move all sub-attributes inside
  // Then move back "value", "type" to the attribute
  for (KjNode* attrP = v2EntityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type != KjObject)  // attributes are objects, "id", "type", etc, are not
    {
      if (strcmp(attrP->name, "type") == 0)
      {
        if (compact == true)
        {
          eqForDot(attrP->value.s);
          attrP->value.s = orionldContextItemAliasLookup(contextP, attrP->value.s, NULL, NULL);
        }
        else
          attrP->value.s = orionldContextItemExpand(contextP, attrP->value.s, true, NULL);
      }

      continue;
    }

    eqForDot(attrP->name);
    if (compact == true)
      attrP->name = orionldContextItemAliasLookup(orionldState.contextP, attrP->name, NULL, NULL);

    // Turn object, languageMap to 'value'
    KjNode* objectP      = kjLookup(attrP, "object");
    KjNode* languageMapP = kjLookup(attrP, "languageMap");

    if (objectP      != NULL)  objectP->name      = (char*) "value";
    if (languageMapP != NULL)  languageMapP->name = (char*) "value";

    if (keyValues)
    {
      KjNode* valueP = kjLookup(attrP, "value");

      if (valueP != NULL)
      {
        attrP->type  = valueP->type;
        attrP->value = valueP->value;
      }

      continue;
    }

    // Got an attribute
    // - create a "metadata" object for the attribute
    // - move all sub-attributes inside "metadata"
    KjNode* metadataObjectP = kjObject(orionldState.kjsonP, "metadata");
    KjNode* mdP = attrP->value.firstChildP;
    KjNode* next;

    while (mdP != NULL)
    {
      if (mdP->type != KjObject)
      {
        mdP = mdP->next;
        continue;
      }
      next = mdP->next;

      // Turn object, languageMap to 'value'
      KjNode* objectP      = kjLookup(mdP, "object");
      KjNode* languageMapP = kjLookup(mdP, "languageMap");

      if (objectP      != NULL)  objectP->name      = (char*) "value";
      if (languageMapP != NULL)  languageMapP->name = (char*) "value";

      if (strcmp(mdP->name, "value") != 0)
      {
        kjChildRemove(attrP, mdP);
        kjChildAdd(metadataObjectP, mdP);
      }

      eqForDot(mdP->name);
      if (compact)
        mdP->name = orionldContextItemAliasLookup(orionldState.contextP, mdP->name, NULL, NULL);

      mdP = next;
    }

    kjChildAdd(attrP, metadataObjectP);
  }

  return v2EntityP;
}



// -----------------------------------------------------------------------------
//
// attributeFilter -
//
static KjNode* attributeFilter(KjNode* apiEntityP, KjNode* attributesP)
{
  KjNode* filteredEntityP = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrP           = apiEntityP->value.firstChildP;
  KjNode* next;

  while (attrP != NULL)
  {
    next = attrP->next;

    bool clone = false;
    if      (strcmp(attrP->name, "id")   == 0) clone = true;
    else if (strcmp(attrP->name, "type") == 0) clone = true;
    else
    {
      char dotName[512];
      strncpy(dotName, attrP->name, sizeof(dotName) - 1);
      eqForDot(dotName);

      for (KjNode* wantedP = attributesP->value.firstChildP; wantedP != NULL; wantedP = wantedP->next)
      {
        if (strcmp(dotName, wantedP->value.s) == 0)
        {
          clone = true;
          KT_T(KtShowChanges, "Adding the attribute '%s' to a notification entity - add also the previousValue!", wantedP->value.s);
          break;
        }
      }
    }

    if (clone)
    {
      KjNode* nodeP = kjClone(orionldState.kjsonP, attrP);
      kjChildAdd(filteredEntityP, nodeP);
    }

    attrP = next;
  }

  return filteredEntityP;
}



// -----------------------------------------------------------------------------
//
// datasetIdInList - is 'datasetId' in the subscription's datasetId filter?
//
static bool datasetIdInList(const char* datasetId, KjNode* datasetIdP)
{
  if (datasetIdP->type == KjString)
    return (strcmp(datasetIdP->value.s, datasetId) == 0);

  for (KjNode* dsP = datasetIdP->value.firstChildP; dsP != NULL; dsP = dsP->next)
  {
    if ((dsP->type == KjString) && (strcmp(dsP->value.s, datasetId) == 0))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// datasetFilter -
//
// Project each attribute to only the dataset instance(s) whose 'datasetId' is in
// the subscription's top-level 'datasetId' filter (so a goal-scoped subscription
// notifies that goal's instance, not the default / other goals).
//
// Returns a CLONE - 'apiEntityP' (the alteration's finalApiEntityP) is shared
// across subscriptions and must not be mutated in place.
//
static KjNode* datasetFilter(KjNode* apiEntityP, KjNode* datasetIdP)
{
  KjNode* outP  = kjClone(orionldState.kjsonP, apiEntityP);
  KjNode* attrP = outP->value.firstChildP;
  KjNode* next;

  while (attrP != NULL)
  {
    next = attrP->next;

    if ((strcmp(attrP->name, "id") == 0) || (strcmp(attrP->name, "type") == 0))
    {
      attrP = next;
      continue;
    }

    if (attrP->type == KjArray)
    {
      KjNode* instP = attrP->value.firstChildP;
      KjNode* instNext;

      while (instP != NULL)
      {
        instNext = instP->next;

        KjNode*     dsP = kjLookup(instP, "datasetId");
        const char* ds  = (dsP != NULL)? dsP->value.s : "@none";

        if (datasetIdInList(ds, datasetIdP) == false)
          kjChildRemove(attrP, instP);

        instP = instNext;
      }

      if (attrP->value.firstChildP == NULL)                      // no instance matched -> drop the attribute
        kjChildRemove(outP, attrP);
      else if (attrP->value.firstChildP->next == NULL)           // one instance left -> flatten to object
      {
        attrP->value = attrP->value.firstChildP->value;
        attrP->type  = KjObject;
      }
    }
    else if (attrP->type == KjObject)
    {
      KjNode*     dsP = kjLookup(attrP, "datasetId");
      const char* ds  = (dsP != NULL)? dsP->value.s : "@none";

      if (datasetIdInList(ds, datasetIdP) == false)
        kjChildRemove(outP, attrP);
    }

    attrP = next;
  }

  return outP;
}



// -----------------------------------------------------------------------------
//
// notificationTreeForNgsiV2 -
//
static KjNode* notificationTreeForNgsiV2(OrionldAlterationMatch* matchP)
{
  SubCacheItem*  subP                 = matchP->subP;
  KjNode*        notificationP        = kjObject(orionldState.kjsonP, NULL);
  KjNode*        subscriptionIdNodeP  = kjString(orionldState.kjsonP, "subscriptionId", subP->subId);
  KjNode*        dataNodeP            = kjArray(orionldState.kjsonP,  "data");
  KjNode*        attributesP          = subNotificationMember(subP, "attributes");
  bool           keyValues            = false;
  bool           compact              = false;

  //
  // Filter out unwanted attributes, if so requested (by the Subscription)
  //
  KjNode* apiEntityP = matchP->altP->finalApiEntityP;  // This is not correct - can be more than one entity

  if (attributesP != NULL)
    apiEntityP = attributeFilter(apiEntityP, attributesP);

  if ((subP->renderFormat == RF_CROSS_APIS_SIMPLIFIED) || (subP->renderFormat == RF_CROSS_APIS_SIMPLIFIED_COMPACT))
    keyValues = true;

  if ((subP->renderFormat == RF_CROSS_APIS_NORMALIZED_COMPACT) || (subP->renderFormat == RF_CROSS_APIS_SIMPLIFIED_COMPACT))
    compact = true;

  KjNode* ngsiv2EntityP = orionldEntityToNgsiV2(subP->contextP, apiEntityP, keyValues, compact);

  kjChildAdd(dataNodeP, ngsiv2EntityP);  // Adding only the first one ...
  kjChildAdd(notificationP, dataNodeP);
  kjChildAdd(notificationP, subscriptionIdNodeP);

  return notificationP;
}



// -----------------------------------------------------------------------------
//
// notificationTree -
//
static KjNode* notificationTree(OrionldAlterationMatch* matchList)
{
  SubCacheItem* subP          = matchList->subP;
  KjNode*       notificationP = kjObject(orionldState.kjsonP, NULL);
  KjNode*       attributesP   = subNotificationMember(subP, "attributes");
  KjNode*       datasetIdP    = kjLookup(subP->subTree, "datasetId");
  char          notificationId[80];

  uuidGenerate(notificationId, sizeof(notificationId), "urn:ngsi-ld:Notification:");  // notificationId could be a thread variable ...

  KjNode* idNodeP              = kjString(orionldState.kjsonP, "id", notificationId);
  KjNode* typeNodeP            = kjString(orionldState.kjsonP, "type", "Notification");
  KjNode* subscriptionIdNodeP  = kjString(orionldState.kjsonP, "subscriptionId", subP->subId);
  KjNode* notifiedAtNodeP      = kjString(orionldState.kjsonP, "notifiedAt", orionldState.requestTimeString);
  KjNode* dataNodeP            = kjArray(orionldState.kjsonP,  "data");

  kjChildAdd(notificationP, idNodeP);
  kjChildAdd(notificationP, typeNodeP);
  kjChildAdd(notificationP, subscriptionIdNodeP);
  kjChildAdd(notificationP, notifiedAtNodeP);
  kjChildAdd(notificationP, dataNodeP);

  // Reason for the notification
  if (triggerOperation == true)
  {
    char trigger[128];
    snprintf(trigger, sizeof(trigger) - 1, "%s %s", orionldState.verbString, orionldState.urlPath);
    KjNode* triggerP = kjString(orionldState.kjsonP, "trigger", trigger);
    kjChildAdd(notificationP, triggerP);
  }

  for (OrionldAlterationMatch* matchP = matchList; matchP != NULL; matchP = matchP->next)
  {
    KjNode* apiEntityP = (subP->sysAttrs == false)? matchP->altP->finalApiEntityP : matchP->altP->finalApiEntityWithSysAttrsP;

    KT_T(KtSysAttrs, "sysAttrs:%s, apiEntityP at %p", (subP->sysAttrs == true)? "true" : "false", apiEntityP);
    if (apiEntityP == NULL)
      apiEntityP = matchP->altP->finalApiEntityP;  // Temporary !!!

    // If the entity is already in "data", and, it's not a BATCH Operation, skip - already there
    if ((orionldState.serviceP == NULL) || (orionldState.serviceP->isBatchOp == false))
    {
      KjNode* idP = kjLookup(apiEntityP, "id");
      if (idP == NULL)
        KT_X(1, "Internal Error (notification entity without an id)");
      if (kjEntityIdLookupInEntityArray(dataNodeP, idP->value.s) != NULL)
      {
        KT_T(KtNotificationBody, "Skipping entity '%s'", idP->value.s);
        continue;
      }
    }

    //
    // Filter out unwanted attributes, if so requested (by the Subscription)
    //
    if (attributesP != NULL)
      apiEntityP = attributeFilter(apiEntityP, attributesP);

    //
    // datasetId projection: if the Subscription has a top-level 'datasetId',
    // keep only the matching dataset instance(s) of each attribute.
    //
    if (datasetIdP != NULL)
      apiEntityP = datasetFilter(apiEntityP, datasetIdP);

    apiEntityP = entityFix(apiEntityP, subP);
    kjChildAdd(dataNodeP, apiEntityP);
  }

  if (subP->mimeType == MT_JSONLD)  // Add @context to the entity
  {
    char*   contextUrl   = ((subP->contextP != NULL) && (subP->contextP->url != NULL))? subP->contextP->url : (char*) "http://localhost:80/no/thing";
    KjNode* contextNodeP = kjString(orionldState.kjsonP, "@context", contextUrl);

    kjChildAdd(notificationP, contextNodeP);
  }

  return notificationP;
}



// -----------------------------------------------------------------------------
//
// notificationSend -
//
// writev is used for the notifications.
// The advantage with writev is that is takes as input an array of buffers, meaning there's no
// need to copy the entire payload into one single buffer:
//
// ssize_t writev(int fd, const struct iovec* iov, int iovcnt);
//
// struct iovec
// {
//   void  *iov_base;    /* Starting address */
//   size_t iov_len;     /* Number of bytes to transfer */
// };
//
// To adapt notificationSend to pernot, I need, from the subscription:
// - renderFormat, subId, contextP, mimeType, rest      (all of it in the SubCacheItem)
// - notification::attributes, endpoint::notifierInfo   (in the subscription tree)
// - finalApiEntityP             (notificationTreeForNgsiV2 - that's the output of the query for Pernot)
// - finalApiEntityWithSysAttrsP (notificationTree - must add sysAttrs to the query if the Pernot sub has sysAttrs set)
//
int notificationSend(OrionldAlterationMatch* mAltP, double timestamp, CURL** curlHandlePP)
{
  SubCacheItem* subP       = mAltP->subP;
  bool          ngsiv2     = (subP->renderFormat >= RF_CROSS_APIS_NORMALIZED);
  KjNode*       receiverP  = subEndpointMember(subP, "receiverInfo");

  //
  // The Subscription's own @context, for the Link header and for GeoJSON. A
  // subscription without a "jsonldContext" has none - the core context is used.
  //
  const char* subContext = ((subP->contextP != NULL) && (subP->contextP->url != NULL))? subP->contextP->url : NULL;

  // <DEBUG>
  if (ktTraceLevelCheck(KtAlt) == true)
  {
    for (OrionldAlterationMatch* mP = mAltP; mP != NULL; mP = mP->next)
    {
      KT_T(KtAlt, "AlterationMatch %p", mP);
      KT_T(KtAlt, "  Subscription     %s", mP->subP->subId);
      KT_T(KtAlt, "  Entity:          %s", mP->altP->entityId);
      KT_T(KtAlt, "  inEntityP:       %p", mP->altP->inEntityP);
      KT_T(KtAlt, "  finalApiEntityP: %p", mP->altP->finalApiEntityP);
      KT_T(KtAlt, "- - - - - -");
    }
  }
  // </DEBUG>

  //
  // Outgoing Payload Body
  //
  KjNode* notificationP = (ngsiv2 == false)? notificationTree(mAltP) : notificationTreeForNgsiV2(mAltP);
  char*   preferHeader  = NULL;

  if ((ngsiv2 == false) && (subP->mimeType == MT_GEOJSON))
  {
    KjNode* geoqP            = kjLookup(subP->subTree, "geoQ");
    KjNode* geopropertyP     = (geoqP != NULL)? kjLookup(geoqP, "geoproperty") : NULL;
    char*   geometryProperty = (geopropertyP != NULL)? geopropertyP->value.s : NULL;
    char*   attrs            = NULL;
    bool    concise          = (subP->renderFormat == RF_CONCISE);

    if ((geometryProperty == NULL) || (geometryProperty[0] == 0))
      geometryProperty = (char*) "location";

    preferHeader = keyValueLookup(subEndpointMember(subP, "notifierInfo"), "Prefer");

    notificationDataToGeoJson(notificationP, attrs, geometryProperty, preferHeader, concise, subContext);
  }

  long unsigned int  payloadBodySize  = kjFastRenderSize(notificationP);
  char*              payloadBody      = kaAlloc(&orionldState.kalloc, payloadBodySize + 512);

  kjFastRender(notificationP, payloadBody);


  //
  // Preparing the HTTP headers which will be pretty much the same for all notifications
  // What differs is Content-Length, Content-Type, and the Request header
  //

  //
  // Outgoing Header
  //
  char    requestHeader[512];
  size_t  requestHeaderLen = 0;

  if (subP->protocol == HTTP)
  {
    // The slash before the URL (rest) is needed as it was removed in "urlParse" in orionld/common/urlParse.cpp
    if (subP->renderFormat < RF_CROSS_APIS_NORMALIZED)
      requestHeaderLen = snprintf(requestHeader, sizeof(requestHeader), "POST /%s?subscriptionId=%s HTTP/1.1\r\n", subP->rest, subP->subId);
    else
      requestHeaderLen = snprintf(requestHeader, sizeof(requestHeader), "POST /%s HTTP/1.1\r\n", subP->rest);

    KT_T(KtNotificationSend, "%s: URL PATH for notification == '%s'", subP->subId, subP->rest);
  }

  //
  // Content-Length
  //
  char              contentLenHeader[32];
  char*             lenP           = &contentLenHeader[16];
  int               sizeLeftForLen = 16;                   // 16: sizeof(contentLenHeader) - 16
  long unsigned int contentLength  = strlen(payloadBody);  // FIXME: kjFastRender should return the size

  strcpy(contentLenHeader, "Content-Length: 0");  // Can't modify inside static strings, so need a char-vec on the stack for contentLenHeader
  snprintf(lenP, sizeLeftForLen, "%d\r\n", (int) contentLength);  // Adding Content-Length inside contentLenHeader

  int headers  = 7;  // the minimum number of request headers


  //
  // Headers to be forwarded in notifications (taken from the request that provoked the notification)
  //
  if (orionldState.in.tenant        != NULL)    ++headers;
  if (orionldState.in.xAuthToken    != NULL)    ++headers;
  if (orionldState.in.authorization != NULL)    ++headers;


  //
  // Headers from Subscription::notification::endpoint::receiverInfo+headers (or custom notification in NGSIv2 ...)
  //
  if (receiverP != NULL)
  {
    for (KjNode* kvP = receiverP->value.firstChildP; kvP != NULL; kvP = kvP->next)
      ++headers;
  }


  // Let's limit the number of headers to 50
  if (headers > 50)
    KT_X(1, "Too many HTTP headers (>50) for a Notification - to support that many, the broker needs a SW update and to be recompiled");

  char          hostHeader[512];
  size_t        hostHeaderLen;

  //
  // A WS endpoint has no host:port - its URI names the WebSocket the subscription
  // was created on, and that is what the receiver is told.
  //
  if ((subP->protocol == WS) || (subP->protocol == WSS))
    hostHeaderLen = snprintf(hostHeader, sizeof(hostHeader), "Host: %s\r\n", subP->url);
  else
    hostHeaderLen = snprintf(hostHeader, sizeof(hostHeader), "Host: %s:%d\r\n", subP->ip, subP->port);

  int           ioVecLen   = headers + 3;  // Request line + X headers + empty line + payload body
  int           headerIx   = 7;
  struct iovec  ioVec[53]  = {
    { requestHeader,                 requestHeaderLen },
    { contentLenHeader,              strlen(contentLenHeader) },
    { (void*) contentTypeHeaderJson, 32 },  // Index 2
    { (void*) userAgentHeader,       userAgentHeaderLen },
    { (void*) hostHeader,            hostHeaderLen },
    { (void*) acceptHeader,          26 },
    { (void*) normalizedHeader,      37 }   // Index 6
  };

  //
  // Content-Type and Link
  //
  bool addLinkHeader = true;

  if (preferHeader != NULL)
  {
    if (strcmp(preferHeader, "body=json") == 0)
      addLinkHeader = false;
  }

  if (subP->mimeType == MT_JSONLD)  // If Content-Type is application/ld+json, modify slot 2 of ioVec
  {
    ioVec[2].iov_base = (void*) contentTypeHeaderJsonLd;  // REPLACE "application/json" with "application/ld+json"
    ioVec[2].iov_len  = 35;
    addLinkHeader     = false;
  }
  else if (subP->mimeType == MT_GEOJSON)
  {
    ioVec[2].iov_base = (void*) contentTypeHeaderGeoJson;  // REPLACE "application/json" with "application/geo+json"
    ioVec[2].iov_len  = 36;
  }

  if ((addLinkHeader == true) && (ngsiv2 == false))  // Add Link header - but not if NGSIv2 Cross Notification
  {
    char         linkHeader[512];
    const char*  link = (subContext == NULL)? orionldCoreContextP->url : subContext;

    snprintf(linkHeader, sizeof(linkHeader), "Link: <%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"\r\n", link);

    ioVec[headerIx].iov_base = linkHeader;
    ioVec[headerIx].iov_len  = strlen(linkHeader);
    ++headerIx;
  }

  //
  // Ngsild-Attribute-Format / Ngsiv1-Attrsformat
  //
  if (subP->renderFormat == RF_CONCISE)
  {
    ioVec[6].iov_base = (void*) conciseHeader;
    ioVec[6].iov_len  = 34;
  }
  else if (subP->renderFormat == RF_SIMPLIFIED)
  {
    ioVec[6].iov_base = (void*) simplifiedHeader;
    ioVec[6].iov_len  = 37;
  }
  else if ((subP->renderFormat == RF_CROSS_APIS_NORMALIZED) || (subP->renderFormat == RF_CROSS_APIS_NORMALIZED_COMPACT))
  {
    ioVec[6].iov_base = (void*) normalizedHeaderNgsiV2;
    ioVec[6].iov_len  = 32;
  }
  else if ((subP->renderFormat == RF_CROSS_APIS_SIMPLIFIED) || (subP->renderFormat == RF_CROSS_APIS_SIMPLIFIED_COMPACT))
  {
    ioVec[6].iov_base = (void*) keyValuesHeaderNgsiV2;
    ioVec[6].iov_len  = 31;
  }


  //
  // Ngsild-Tenant
  //
  if ((orionldState.in.tenant != NULL) && (ngsiv2 == false))
  {
    int   len = strlen(orionldState.in.tenant) + 20;  // Ngsild-Tenant: xxx\r\n0 - '\r' seems to not count for strlen ...
    char* buf = kaAlloc(&orionldState.kalloc, len);

    ioVec[headerIx].iov_len  = snprintf(buf, len, "Ngsild-Tenant: %s\r\n", orionldState.in.tenant);
    ioVec[headerIx].iov_base = buf;

    ++headerIx;
  }

  //
  // FIXME: Store headers in a better way - see issue #1095
  //
  bool authorizationHeaderPresent = false;
  bool xAuthTokenPresent          = false;

  for (KjNode* kvP = (receiverP != NULL)? receiverP->value.firstChildP : NULL; kvP != NULL; kvP = kvP->next)
  {
    KjNode* keyNodeP   = kjLookup(kvP, "key");
    KjNode* valueNodeP = kjLookup(kvP, "value");

    if ((keyNodeP == NULL) || (valueNodeP == NULL) || (valueNodeP->type != KjString))
      continue;

    const char* key    = keyNodeP->value.s;
    char*       value  = valueNodeP->value.s;

    if (strcmp(value, "urn:ngsi-ld:request") == 0)
    {
      if (orionldState.in.httpHeaders != NULL)
      {
        KjNode* kvP = kjLookup(orionldState.in.httpHeaders, key);
        if ((kvP != NULL) && (kvP->type == KjString))
          value = kvP->value.s;
        else
          continue;  // Not found in initial request - ignoring the header
      }
      else
        continue;  // No incoming headers?  Must ignore the urn:ngsi-ld:request header, no other choice
    }

    int   len = strlen(key) + strlen(value) + 10;
    char* buf = kaAlloc(&orionldState.kalloc, len);

    if (strcasecmp(key, "Authorization") == 0)
      authorizationHeaderPresent = true;
    if (strcasecmp(key, "X-Auth-Token") == 0)
      xAuthTokenPresent = true;

    ioVec[headerIx].iov_len  = snprintf(buf, len, "%s: %s\r\n", key, value);
    ioVec[headerIx].iov_base = buf;
    ++headerIx;
  }

  //
  // Automatic inclusion of the X-Auth-Token header
  // But, only if it NOT part of "receiverInfo" of the subscription
  //
  if ((xAuthTokenPresent == false) && (orionldState.in.xAuthToken != NULL))
  {
    int   len = strlen(orionldState.in.xAuthToken) + 20;  // X-Auth-Token: xxx\r\n0
    char* buf = kaAlloc(&orionldState.kalloc, len);

    ioVec[headerIx].iov_len  = snprintf(buf, len, "X-Auth-Token: %s\r\n", orionldState.in.xAuthToken);
    ioVec[headerIx].iov_base = buf;
    ++headerIx;
  }

  //
  // Automatic inclusion of the Authorization header
  // But, only if it NOT part of "receiverInfo" of the subscription
  //
  if ((authorizationHeaderPresent == false) && (orionldState.in.authorization != NULL))
  {
    int   len = strlen(orionldState.in.authorization) + 20;  // Authorization: xxx\r\n0
    char* buf = kaAlloc(&orionldState.kalloc, len);

    ioVec[headerIx].iov_len  = snprintf(buf, len, "Authorization: %s\r\n", orionldState.in.authorization);
    ioVec[headerIx].iov_base = buf;
    ++headerIx;
  }

  // Empty line delimiting HTTP Headers and Payload Body
  ioVec[headerIx].iov_base = (void*) "\r\n";
  ioVec[headerIx].iov_len  = 2;
  ++headerIx;


  // Payload Body
  ioVec[headerIx].iov_base = payloadBody;
  ioVec[headerIx].iov_len  = contentLength;

  ioVecLen = headerIx + 1;

  //
  // The message is ready - just need to be sent
  //
  if (subP->protocol == HTTP)
    return httpNotify(subP, NULL, subP->subId, subP->ip, subP->port, subP->rest, ioVec, ioVecLen, timestamp);
  else if (subP->protocol == HTTPS)   return httpsNotify(subP, ioVec, ioVecLen, timestamp, curlHandlePP);
  else if (subP->protocol == MQTT)    return mqttNotify(subP,  ioVec, ioVecLen, timestamp);
  else if (subP->protocol == MQTTS)   return mqttNotify(subP,  ioVec, ioVecLen, timestamp);
  else if (subP->protocol == WS)      return wsNotify(subP,    ioVec, ioVecLen, timestamp);

  KT_W("%s: Unsupported protocol for notifications: '%s'", subP->subId, subP->protocolString);
  return -1;
}
