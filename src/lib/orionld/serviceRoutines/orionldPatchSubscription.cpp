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
#include <string>                                              // std::string
#include <vector>                                              // std::vector

extern "C"
{
#include "kalloc/kaAlloc.h"                                    // kaAlloc
#include "kalloc/kaStrdup.h"                                   // kaStrdup
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjChildAdd, ...
#include "kjson/kjClone.h"                                     // kjClone
#include "kjson/kjFree.h"                                      // kjFree
#include "kjson/kjRenderSize.h"                                // kjFastRenderSize
#include "kjson/kjRender.h"                                    // kjFastRender
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "cache/subCache.h"                                    // CachedSubscription, subCacheItemLookup
#include "common/globals.h"                                    // parse8601Time

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldError.h"                       // orionldError
#include "orionld/common/urlParse.h"                           // urlParse
#include "orionld/common/mimeTypeFromString.h"                 // mimeTypeFromString
#include "orionld/types/KeyValue.h"                            // KeyValue, keyValueLookup, keyValueAdd
#include "orionld/types/MqttInfo.h"                            // MqttInfo
#include "orionld/context/orionldAttributeExpand.h"            // orionldAttributeExpand
#include "orionld/payloadCheck/PCHECK.h"                       // PCHECK_URI
#include "orionld/payloadCheck/pcheckSubscription.h"           // pcheckSubscription
#include "orionld/mongoc/mongocSubscriptionLookup.h"           // mongocSubscriptionLookup
#include "orionld/mongoc/mongocSubscriptionReplace.h"          // mongocSubscriptionReplace
#include "orionld/dbModel/dbModelFromApiSubscription.h"        // dbModelFromApiSubscription
#include "orionld/mqtt/mqttConnectionEstablish.h"              // mqttConnectionEstablish
#include "orionld/mqtt/mqttDisconnect.h"                       // mqttDisconnect
#include "orionld/mqtt/mqttParse.h"                            // mqttParse
#include "orionld/kjTree/kjTreeLog.h"                          // kjTreeLog
#include "orionld/kjTree/kjNavigate.h"                         // kjNavigate
#include "orionld/kjTree/kjChildAddOrReplace.h"                // kjChildAddOrReplace
#include "orionld/serviceRoutines/orionldPatchSubscription.h"  // Own Interface



// ----------------------------------------------------------------------------
//
// okToRemove -
//
static bool okToRemove(const char* fieldName)
{
  if ((strcmp(fieldName, "id") == 0) || (strcmp(fieldName, "@id") == 0))
    return false;
  else if (strcmp(fieldName, "notification") == 0)
    return false;
  else if (strcmp(fieldName, "status") == 0)
    return false;

  return true;
}



// ----------------------------------------------------------------------------
//
// ngsildSubscriptionPatch -
//
// The 'q' and 'geoQ' of an NGSI-LD comes in like this:
// {
//   "q": "",
//   "geoQ": {
//     "geometry": "",
//     ""
// }
//
// In the DB, 'q' and 'geoQ' are inside "expression":
// {
//   "expression" : {
//     "q" : "https://uri=etsi=org/ngsi-ld/default-context/P2>10",
//     "mq" : "",
//     "geometry" : "circle",
//     "coords" : "1,2",
//     "georel" : "near"
//   }
// }
//
// So, if "geoQ" is present in the patch tree, then "geoQ" replaces "expression",
// by simply changing its name from "geoQ" to "expression".
// DON'T forget the "q", that is also part of "expression" but not a part of "geoQ".
// If "geoQ" replaces "expression", then we may need to maintain the "q" inside the old "expression".
// OR, if "q" is also in the patch tree, then we'll simply move it inside "expression" (former "geoQ").
//
static bool ngsildSubscriptionPatch(KjNode* dbSubscriptionP, CachedSubscription* cSubP, KjNode* patchTree, KjNode* qP, KjNode* expressionP)
{
  KjNode* fragmentP = patchTree->value.firstChildP;
  KjNode* next;

  while (fragmentP != NULL)
  {
    next = fragmentP->next;

    if (fragmentP->type == KjNull)
    {
      KjNode* toRemove = kjLookup(dbSubscriptionP, fragmentP->name);

      if (toRemove != NULL)
      {
        if (okToRemove(fragmentP->name) == false)
        {
          orionldError(OrionldBadRequestData, "Invalid Subscription Fragment - attempt to remove a mandatory field", fragmentP->name, 400);
          return false;
        }

        kjChildRemove(dbSubscriptionP, toRemove);
      }
    }
    else
    {
      if ((fragmentP != qP) && (fragmentP != expressionP))
        kjChildAddOrReplace(dbSubscriptionP, fragmentP->name, fragmentP);

      if (strcmp(fragmentP->name, "status") == 0)
      {
        if (strcmp(fragmentP->value.s, "active") == 0)
        {
          cSubP->isActive = true;
          cSubP->status   = "active";
        }
        else
        {
          cSubP->isActive = false;
          cSubP->status   = "paused";
        }
      }
    }

    fragmentP = next;
  }


  //
  // If geoqP/expressionP != NULL, then it replaces the "expression" in the DB
  // If also qP != NULL, then this qP is added to geoqP/expressionP
  // If not, we have to lookup 'q' in the old "expression" and add it to geoqP/expressionP
  //
  if (expressionP != NULL)
  {
    KjNode* dbExpressionP = kjLookup(dbSubscriptionP, "expression");

    if (dbExpressionP != NULL)
      kjChildRemove(dbSubscriptionP, dbExpressionP);
    kjChildRemove(patchTree, expressionP);
    kjChildAdd(dbSubscriptionP, expressionP);

    //
    // If 'q' is not present in the patch tree, and we have replaced the 'expression', then
    // we need to get the 'q' from the old 'expression' and add it to the new expression.
    //
    if ((qP == NULL) && (dbExpressionP != NULL))
      qP = kjLookup(dbExpressionP, "q");

    if (qP != NULL)
      kjChildAdd(expressionP, qP);
  }
  else if (qP != NULL)
  {
    KjNode* dbExpressionP = kjLookup(dbSubscriptionP, "expression");

    if (dbExpressionP != NULL)
      kjChildAddOrReplace(dbExpressionP, "q", qP);
    else
    {
      // A 'q' has been given but there is no "expression" - need to create one
      expressionP = kjObject(orionldState.kjsonP, "expression");

      kjChildAdd(expressionP, qP);
      kjChildAdd(dbSubscriptionP, expressionP);
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// fixDbSubscription -
//
// As long long members are respresented as "xxx": { "$numberLong": "1234565678901234" }
// and this gives an error when trying to Update this, we simply change the object to an int.
//
// In a Subscription, this must be done for "expiration", and "throttling".
//
static void fixDbSubscription(KjNode* dbSubscriptionP)
{
  KjNode* nodeP;

  //
  // If 'expiration' is an Object, it means it's a NumberLong and it is then changed to a double
  //
  if ((nodeP = kjLookup(dbSubscriptionP, "expiration")) != NULL)
  {
    if (nodeP->type == KjObject)
    {
      char*      expirationString = nodeP->value.firstChildP->value.s;
      double     expiration       = strtold(expirationString, NULL);

      nodeP->type    = KjFloat;
      nodeP->value.f = expiration;
    }
  }

  //
  // If 'throttling' is an Object, it means it's a NumberLong and it is then changed to a double
  //
  if ((nodeP = kjLookup(dbSubscriptionP, "throttling")) != NULL)
  {
    if (nodeP->type == KjObject)
    {
      char*      throttlingString = nodeP->value.firstChildP->value.s;
      long long  throttling       = strtold(throttlingString, NULL);

      nodeP->type    = KjFloat;
      nodeP->value.f = throttling;
    }
  }
}



// -----------------------------------------------------------------------------
//
// subCacheItemUpdateEntities -
//
static bool subCacheItemUpdateEntities(CachedSubscription* cSubP, KjNode* entityArray)
{
  cSubP->entityIdInfos.empty();
  for (KjNode* entityP = entityArray->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    KjNode*      idP          = kjLookup(entityP, "id");
    KjNode*      idPatternP   = kjLookup(entityP, "idPattern");
    KjNode*      typeP        = kjLookup(entityP, "type");
    EntityInfo*  entityInfoP;

    if ((idP == NULL) && (idPatternP == NULL))
      entityInfoP = new EntityInfo(".*", std::string(typeP->value.s), "true", false);
    else if (idP != NULL)
      entityInfoP = new EntityInfo(std::string(idP->value.s), std::string(typeP->value.s), "false", false);
    else
      entityInfoP = new EntityInfo(std::string(idPatternP->value.s), std::string(typeP->value.s), "true", false);

    cSubP->entityIdInfos.push_back(entityInfoP);
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// subCacheItemUpdateWatchedAttributes -
//
static bool subCacheItemUpdateWatchedAttributes(CachedSubscription* cSubP, KjNode* itemP)
{
  cSubP->notifyConditionV.empty();

  int  attrs = 0;
  for (KjNode* attrNodeP = itemP->value.firstChildP; attrNodeP != NULL; attrNodeP = attrNodeP->next)
  {
    char* longName = orionldAttributeExpand(orionldState.contextP, attrNodeP->value.s, true, NULL);

    cSubP->notifyConditionV.push_back(longName);
    ++attrs;
  }

  if ((int) cSubP->notifyConditionV.size() != attrs)
    LM_RE(false, ("Expected %d items in watchedAttributes list - there are %d !!!", attrs, cSubP->notifyConditionV.size()));

  return true;
}



// -----------------------------------------------------------------------------
//
// subCacheItemUpdateGeoQ -
//
static bool subCacheItemUpdateGeoQ(CachedSubscription* cSubP, KjNode* itemP)
{
  KjNode* geometryP     = kjLookup(itemP, "geometry");
  KjNode* coordinatesP  = kjLookup(itemP, "coordinates");
  KjNode* georelP       = kjLookup(itemP, "georel");
  KjNode* geopropertyP  = kjLookup(itemP, "geoproperty");

  if (geometryP    != NULL) cSubP->expression.geometry    = geometryP->value.s;
  if (georelP      != NULL) cSubP->expression.georel      = georelP->value.s;
  if (geopropertyP != NULL) cSubP->expression.geoproperty = geopropertyP->value.s;

  if (coordinatesP != NULL)
  {
    int   coordsSize = kjFastRenderSize(coordinatesP) + 100;
    char* coords     = kaAlloc(&orionldState.kalloc, coordsSize);

    kjFastRender(coordinatesP, coords);
    cSubP->expression.coords = coords;  // Not sure this is 100% correct format, but is it used? DB is used for Geo ...
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// subCacheItemUpdateNotificationEndpoint -
//
static bool subCacheItemUpdateNotificationEndpoint(CachedSubscription* cSubP, KjNode* endpointP)
{
  KjNode* uriP           = kjLookup(endpointP, "uri");
  KjNode* acceptP        = kjLookup(endpointP, "accept");
  KjNode* receiverInfoP  = kjLookup(endpointP, "receiverInfo");
  KjNode* notifierInfoP  = kjLookup(endpointP, "notifierInfo");

  if (uriP != NULL)
  {
    char*           protocol;
    char*           ip;
    unsigned short  port;
    char*           rest;
    char*           url = strdup(uriP->value.s);

    if (strncmp(uriP->value.s, "mqtt", 4) == 0)
    {
      char*           url           = strdup(uriP->value.s);
      bool            mqtts         = false;
      char*           mqttUser      = NULL;
      char*           mqttPassword  = NULL;
      char*           mqttHost      = NULL;
      unsigned short  mqttPort      = 0;
      char*           mqttTopic     = NULL;
      char*           detail        = NULL;

      if (mqttParse(url, &mqtts, &mqttUser, &mqttPassword, &mqttHost, &mqttPort, &mqttTopic, &detail) == false)
      {
        free(url);
        LM_E(("Internal Error (unable to parse mqtt URL)"));
        return false;
      }

      if (mqttUser     != NULL) strncpy(cSubP->httpInfo.mqtt.username, mqttUser,     sizeof(cSubP->httpInfo.mqtt.username) - 1);
      if (mqttPassword != NULL) strncpy(cSubP->httpInfo.mqtt.password, mqttPassword, sizeof(cSubP->httpInfo.mqtt.password) - 1);
      if (mqttHost     != NULL) strncpy(cSubP->httpInfo.mqtt.host,     mqttHost,     sizeof(cSubP->httpInfo.mqtt.host) - 1);
      if (mqttTopic    != NULL) strncpy(cSubP->httpInfo.mqtt.topic,    mqttTopic,    sizeof(cSubP->httpInfo.mqtt.topic) - 1);

      cSubP->httpInfo.mqtt.mqtts = mqtts;
      cSubP->httpInfo.mqtt.port  = mqttPort;

      free(url);
    }
    else
    {
      if (urlParse(url, &protocol, &ip, &port, &rest) == true)
      {
        free(cSubP->url);
        cSubP->url      = url;  // Only ... it's destroyed :)
        cSubP->protocol = protocol;
        cSubP->ip       = ip;
        cSubP->port     = port;
        cSubP->rest     = rest;
      }
      else
      {
        LM_W(("Invalid url '%s'", uriP->value.s));
        return false;
      }
    }
  }

  if (acceptP != NULL)
  {
    uint32_t acceptMask;
    cSubP->httpInfo.mimeType = mimeTypeFromString(acceptP->value.s, NULL, true, false, &acceptMask);
  }

  if (receiverInfoP != NULL)
  {
    //
    // receiverInfo is stored in cSubP->httpInfo.headers
    // - just set it (in the std::map)
    //
    for (KjNode* riP = receiverInfoP->value.firstChildP; riP != NULL; riP = riP->next)
    {
      KjNode*   keyP   = kjLookup(riP, "key");
      KjNode*   valueP = kjLookup(riP, "value");

      cSubP->httpInfo.headers[keyP->value.s] = valueP->value.s;
    }
  }

  if (notifierInfoP != NULL)
  {
    //
    // For each key-value pair in "notifierInfo":
    //   - lookup the key in cSubP->httpInfo.notifierInfo
    //   - replace the value if present, add it if not
    //
    for (KjNode* riP = notifierInfoP->value.firstChildP; riP != NULL; riP = riP->next)
    {
      KjNode*   keyP   = kjLookup(riP, "key");
      KjNode*   valueP = kjLookup(riP, "value");
      KeyValue* kvP    = keyValueLookup(cSubP->httpInfo.notifierInfo, keyP->value.s);

      if (kvP != NULL)
        strncpy(kvP->value, valueP->value.s, sizeof(kvP->value) - 1);
      else
        keyValueAdd(&cSubP->httpInfo.notifierInfo, keyP->value.s, valueP->value.s);
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// subCacheItemUpdateNotification -
//
static bool subCacheItemUpdateNotification(CachedSubscription* cSubP, KjNode* itemP)
{
  KjNode* attributesP = kjLookup(itemP, "attributes");
  KjNode* formatP     = kjLookup(itemP, "format");
  KjNode* endpointP   = kjLookup(itemP, "endpoint");

  if (attributesP != NULL)  // Those present in the notification
  {
    cSubP->attributes.empty();
    for (KjNode* attrNodeP = attributesP->value.firstChildP; attrNodeP != NULL; attrNodeP = attrNodeP->next)
    {
      char* longName = orionldAttributeExpand(orionldState.contextP, attrNodeP->value.s, true, NULL);
      cSubP->attributes.push_back(longName);
    }
  }

  if (formatP != NULL)
    cSubP->renderFormat = stringToRenderFormat(formatP->value.s);

  if (endpointP != NULL)
    return subCacheItemUpdateNotificationEndpoint(cSubP, endpointP);

  return true;
}



// -----------------------------------------------------------------------------
//
// subCacheItemUpdate -
//
static bool subCacheItemUpdate(OrionldTenant* tenantP, const char* subscriptionId, KjNode* subscriptionTree, KjNode* geoCoordinatesP)
{
  CachedSubscription* cSubP = subCacheItemLookup(tenantP->tenant, subscriptionId);
  bool                r     = true;

  if (cSubP == NULL)
    LM_RE(false, ("Internal Error (can't find the subscription '%s' in the subscription cache)", subscriptionId));

  cacheSemTake(__FUNCTION__, "Updating a cached subscription");
  subCacheState = ScsSynchronizing;

  if (geoCoordinatesP != NULL)
  {
    if (cSubP->geoCoordinatesP != NULL)
      kjFree(cSubP->geoCoordinatesP);
    cSubP->geoCoordinatesP = kjClone(NULL, geoCoordinatesP);
  }

  for (KjNode* itemP = subscriptionTree->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if ((strcmp(itemP->name, "subscriptionName") == 0) || (strcmp(itemP->name, "name") == 0))
      cSubP->name = itemP->value.s;
    else if (strcmp(itemP->name, "description") == 0)
    {
      //
      // The description is only stored in the database ...
      //
      // cSubP->description = itemP->value.s;
    }
    else if (strcmp(itemP->name, "entities") == 0)
    {
      subCacheItemUpdateEntities(cSubP, itemP);
    }
    else if (strcmp(itemP->name, "watchedAttributes") == 0)
    {
      subCacheItemUpdateWatchedAttributes(cSubP, itemP);
    }
    else if (strcmp(itemP->name, "timeInterval") == 0)
    {
      LM_W(("Not Implemented (Orion-LD doesn't implement periodical notifications"));
    }
    else if (strcmp(itemP->name, "lang") == 0)
    {
      cSubP->lang = itemP->value.s;
    }
    else if (strcmp(itemP->name, "q") == 0)
    {
      cSubP->expression.q = itemP->value.s;
    }
    else if (strcmp(itemP->name, "geoQ") == 0)
    {
      subCacheItemUpdateGeoQ(cSubP, itemP);
    }
    else if (strcmp(itemP->name, "csf") == 0)
    {
      //
      // csf is not implemented (only used for Subscriptions on Context Source Registrations)
      //
      // cSubP->csf = itemP->value.s;
    }
    else if (strcmp(itemP->name, "isActive") == 0)
    {
      if (itemP->value.b == true)
      {
        cSubP->isActive = true;
        cSubP->status   = "active";
      }
      else
      {
        cSubP->isActive = false;
        cSubP->status   = "paused";
      }
    }
    else if (strcmp(itemP->name, "notification") == 0)
    {
      subCacheItemUpdateNotification(cSubP, itemP);
    }
    else if ((strcmp(itemP->name, "expires") == 0) || (strcmp(itemP->name, "expiresAt") == 0))
    {
      // Give error for expires/expiresAt and version of NGSI-LD ?
      cSubP->expirationTime = parse8601Time(itemP->value.s);
    }
    else if (strcmp(itemP->name, "throttling") == 0)
    {
      if (itemP->type == KjInt)
        cSubP->throttling = (double) itemP->value.i;
      else if (itemP->type == KjFloat)
        cSubP->throttling = itemP->value.f;
      else
        LM_W(("Invalid type for 'throttling'"));
    }
    else if (strcmp(itemP->name, "scopeQ") == 0)
    {
      LM_W(("Not Implemented (Orion-LD doesn't support Multi-Type (yet)"));
    }
    else if (strcmp(itemP->name, "lang") == 0)
    {
      LM_W(("Not Implemented (Orion-LD doesn't support LanguageProperty just yet"));
    }
    else
    {
      orionldError(OrionldBadRequestData, "Invalid field for subscription patch", itemP->name, 400);
      r = false;
    }
  }

  subCacheState = ScsIdle;
  cacheSemGive(__FUNCTION__, "Updated a cached subscription");
  return r;
}



// -----------------------------------------------------------------------------
//
// mqttInfoFromDbTree -
//
static bool mqttInfoFromDbTree(KjNode* dbSubscriptionP, KjNode* uriP, MqttInfo* miP)
{
  LM_TMP(("URI: '%s'", uriP->value.s));
  char* uri = kaStrdup(&orionldState.kalloc, uriP->value.s);
  char* detail     = NULL;
  char* usernameP  = NULL;
  char* passwordP  = NULL;
  char* hostP      = NULL;
  char* topicP     = NULL;

  LM_TMP(("Calling mqttParse"));
  if (mqttParse(uri, &miP->mqtts, &usernameP, &passwordP, &hostP, &miP->port, &topicP, &detail) == false)
  {
    orionldError(OrionldBadRequestData, "Invalid MQTT endpoint", detail, 400);
    return false;
  }
  LM_TMP(("After mqttParse"));

  if (usernameP != NULL) strncpy(miP->username, usernameP, sizeof(miP->username) - 1);
  if (passwordP != NULL) strncpy(miP->password, passwordP, sizeof(miP->password) - 1);
  if (hostP     != NULL) strncpy(miP->host,     hostP,     sizeof(miP->host)     - 1);
  if (topicP    != NULL) strncpy(miP->topic,    topicP,    sizeof(miP->topic)    - 1);

  LM_TMP(("Topic: '%s'", miP->topic));

  KjNode* notifierInfoP = kjLookup(dbSubscriptionP, "notifierInfo");
  if (notifierInfoP)
  {
    for (KjNode* kvPairP = notifierInfoP->value.firstChildP; kvPairP != NULL; kvPairP = kvPairP->next)
    {
      if      (strcmp(kvPairP->name, "MQTT-Version") == 0)  strncpy(miP->version, kvPairP->value.s, sizeof(miP->version) - 1);
      else if (strcmp(kvPairP->name, "MQTT-QoS")     == 0)  miP->qos = atoi(kvPairP->value.s);
    }
  }

  return true;
}



// ----------------------------------------------------------------------------
//
// mqttConnectFromInfo -
//
static bool mqttConnectFromInfo(MqttInfo* miP)
{
  bool b;

  b = mqttConnectionEstablish(miP->mqtts, miP->username, miP->password, miP->host, miP->port, miP->version);
  LM_TMP(("Connected to MQTT broker at %s:%d", miP->host, miP->port));

  if (b == false)
    LM_RE(false, ("Unable to connect to MQTT broker %s:%s", miP->host, miP->port));

  return true;
}



// ----------------------------------------------------------------------------
//
// mqttDisconnectFromInfo -
//
static void mqttDisconnectFromInfo(MqttInfo* miP)
{
  mqttDisconnect(miP->host, miP->port, miP->username, miP->password, miP->version);
  LM_TMP(("Disconnected from MQTT broker at %s:%d", miP->host, miP->port));
}



// ----------------------------------------------------------------------------
//
// orionldPatchSubscription -
//
// 1. Check that orionldState.wildcard[0] is a valid subscription ID (a URI) - 400 Bad Request ?
// 2. Make sure the payload data is a correct Subscription fragment
//    - No values can be NULL
//    - Expand attribute names ans entity types if present
// 3. GET the subscription from mongo, by calling dbSubscriptionGet(orionldState.wildcard[0])
// 4. If not found - 404
//
// 5. Go over the fragment (incoming payload data) and modify the 'subscription from mongo':
//    * If the provided Fragment (merge patch) contains members that do not appear within the target (their URIs do
//      not match), those members are added to the target.
//    * the target member value is replaced by value given in the Fragment, if non-null values.
//    * If null values in the Fragment, then remove in the target
//
// 6. Call dbSubscriptionReplace(char* subscriptionId, KjNode* subscriptionTree) to replace the old sub with the new
//    Or, dbSubscriptionUpdate(char* subscriptionId, KjNode* toAddP, KjNode* toRemoveP, KjNode* toUpdate)
//
bool orionldPatchSubscription(void)
{
  PCHECK_URI(orionldState.wildcard[0], true, 0, "Subscription ID must be a valid URI", orionldState.wildcard[0], 400);

  char*   subscriptionId         = orionldState.wildcard[0];
  KjNode* watchedAttributesNodeP = NULL;
  KjNode* timeIntervalNodeP      = NULL;
  KjNode* qP                     = NULL;
  KjNode* geoqP                  = NULL;
  KjNode* geoCoordinatesP        = NULL;
  bool    mqttChange             = false;

  if (pcheckSubscription(orionldState.requestTree, false, &watchedAttributesNodeP, &timeIntervalNodeP, &qP, &geoqP, &geoCoordinatesP, true, &mqttChange) == false)
  {
    LM_E(("pcheckSubscription FAILED"));
    return false;
  }

  KjNode* dbSubscriptionP = mongocSubscriptionLookup(subscriptionId);

  if (dbSubscriptionP == NULL)
  {
    orionldError(OrionldResourceNotFound, "Subscription not found", subscriptionId, 404);
    return false;
  }


  //
  // Make sure we don't get both watchedAttributed AND timeInterval
  // If so, the PATCH is invalid
  // Right now, timeInterval is not supported, but once it is, if ever, this code will come in handy
  //
  if (timeIntervalNodeP != NULL)
  {
    orionldError(OrionldBadRequestData, "Not Implemented", "Subscription::timeInterval is not implemented", 501);
    return false;
  }

  if ((watchedAttributesNodeP != NULL) && (timeIntervalNodeP != NULL))
  {
    LM_W(("Bad Input (Both 'watchedAttributes' and 'timeInterval' given in Subscription Payload Data)"));
    orionldError(OrionldBadRequestData, "Invalid Subscription Payload Data", "Both 'watchedAttributes' and 'timeInterval' given", 400);
    return false;
  }
  else if (watchedAttributesNodeP != NULL)
  {
    KjNode* dbTimeIntervalNodeP = kjLookup(dbSubscriptionP, "timeInterval");

    if ((dbTimeIntervalNodeP != NULL) && (dbTimeIntervalNodeP->value.i != -1))
    {
      orionldError(OrionldBadRequestData,
                   "Invalid Subscription Payload Data",
                   "Attempt to set 'watchedAttributes' to a Subscription that is of type 'timeInterval'",
                   400);
      return false;
    }
  }
  else if (timeIntervalNodeP != NULL)
  {
    KjNode* dbConditionsNodeP = kjLookup(dbSubscriptionP, "conditions");

    if ((dbConditionsNodeP != NULL) && (dbConditionsNodeP->value.firstChildP != NULL))
    {
      LM_W(("Bad Input (Attempt to set 'timeInterval' to a Subscription that is of type 'watchedAttributes')"));
      orionldError(OrionldBadRequestData, "Invalid Subscription Payload Data", "Attempt to set 'timeInterval' to a Subscription that is of type 'watchedAttributes'", 400);
      return false;
    }
  }

  //
  // If the subscription used to be an MQTT subscription, the MQTT connection might need closing
  // Only if MQTT data is modified though (or it stops being an MQTT subscription)
  //
  kjTreeLog(dbSubscriptionP, "DB Subscription");
  KjNode*      oldUriP    = kjLookup(dbSubscriptionP, "reference");
  bool         oldWasMqtt = false;
  const char*  uriPath[4] = { "notification", "endpoint", "uri", NULL };
  KjNode*      newUriP    = kjNavigate(orionldState.requestTree, uriPath, NULL, NULL);
  bool         newIsMqtt  = true;
  MqttInfo     oldMqttInfo;
  MqttInfo     newMqttInfo;

  if (oldUriP != NULL)  // Can't really be NULL ...
  {
    if (strncmp(oldUriP->value.s, "mqtt", 4) == 0)
    {
      oldWasMqtt = true;
      bzero(&oldMqttInfo, sizeof(oldMqttInfo));
      mqttInfoFromDbTree(dbSubscriptionP, oldUriP, &oldMqttInfo);
    }
  }

  if (newUriP != NULL)
    newIsMqtt = (strncmp(newUriP->value.s, "mqtt", 4) == 0)? true : false;  // Because the URL has been modified
  else
    newIsMqtt = oldWasMqtt;  // Because the URL has NOT been modified

  if ((oldWasMqtt == true) && (newIsMqtt == false))
    mqttChange = true;  // Because it used to be MQTT but is no more



  //
  // Remove Occurrences of $numberLong, i.e. "expiration"
  //
  // FIXME: This is BAD ... shouldn't change the type of these fields
  //
  fixDbSubscription(dbSubscriptionP);
  KjNode* patchBody = kjClone(orionldState.kjsonP, orionldState.requestTree);

  dbModelFromApiSubscription(orionldState.requestTree, true);

  //
  // After calling dbModelFromApiSubscription, the incoming payload data has beed structured just as the
  // API v1 database model and the original tree (obtained calling dbSubscriptionGet()) can easily be
  // modified.
  // ngsildSubscriptionPatch() performs that modification.
  //
  CachedSubscription* cSubP = subCacheItemLookup(orionldState.tenantP->tenant, subscriptionId);

  if (ngsildSubscriptionPatch(dbSubscriptionP, cSubP, orionldState.requestTree, qP, geoqP) == false)
    LM_RE(false, ("KZ: ngsildSubscriptionPatch failed!"));

  //
  // Update modifiedAt
  //
  KjNode* modifiedAtP = kjLookup(dbSubscriptionP, "modifiedAt");

  if (modifiedAtP != NULL)
    modifiedAtP->value.f = orionldState.requestTime;
  else
  {
    modifiedAtP = kjFloat(orionldState.kjsonP, "modifiedAt", orionldState.requestTime);
    kjChildAdd(dbSubscriptionP, modifiedAtP);
  }

  // Connect to MQTT broker, if needed
  if ((newIsMqtt == true) && (mqttChange == true))
  {
    // GET mix of new and old MQTT info from patched dbSubscriptionP
    bzero(&newMqttInfo, sizeof(newMqttInfo));
    mqttInfoFromDbTree(dbSubscriptionP, newUriP, &newMqttInfo);
    if (mqttConnectFromInfo(&newMqttInfo) == false)
    {
      orionldError(OrionldInternalError, "MQTT Error", "unable to connect to MQTT broker", 500);
      return false;
    }
  }

  // Close the old MQTT connection
  if ((oldWasMqtt == true) && (mqttChange == true))
  {
    // We got the old MQTT connection info from dbSubscriptionP BEFORE it was patched
    mqttDisconnectFromInfo(&oldMqttInfo);
  }

  //
  // Overwrite the current Subscription in the database
  //
  if (mongocSubscriptionReplace(subscriptionId, dbSubscriptionP) == false)
  {
    orionldError(OrionldInternalError, "Database Error", "patching a subscription", 500);
    return false;
  }

  // Modify the subscription in the subscription cache
  if (subCacheItemUpdate(orionldState.tenantP, subscriptionId, patchBody, geoCoordinatesP) == false)
    LM_E(("Internal Error (unable to update the cached subscription '%s' after a PATCH)", subscriptionId));

  // All OK? 204 No Content
  orionldState.httpStatusCode = 204;

  return true;
}
