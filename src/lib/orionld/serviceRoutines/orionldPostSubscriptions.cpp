/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include "kbase/kMacros.h"                                     // K_FT
#include "ktrace/kTrace.h"                                     // KT_*
#include "kalloc/kaStrdup.h"                                   // kaStrdup
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjString, kjChildAdd, ...
#include "kjson/kjClone.h"                                     // kjClone
#include "kjson/kjNavigate.h"                                  // kjNavigate
#include "kjson/kjChildPrepend.h"                              // kjChildPrepend
}

#include "cache/subCache.h"                                    // subCacheItemLookup, CachedSubscription

#include "orionld/types/QNode.h"                               // QNode
#include "orionld/types/PernotSubscription.h"                  // PernotSubscription
#include "orionld/types/PernotSubCache.h"                      // PernotSubCache
#include "orionld/types/RegCache.h"                            // RegCache
#include "orionld/types/RegCacheItem.h"                        // RegCacheItem
#include "orionld/types/SubordinateSubscription.h"             // SubordinateSubscription
#include "orionld/common/orionldState.h"                       // orionldState, coreContextUrl
#include "orionld/common/orionldError.h"                       // orionldError
#include "orionld/common/traceLevels.h"                        // KTrace level
#include "orionld/common/uuidGenerate.h"                       // uuidGenerate
#include "orionld/common/subCacheApiSubscriptionInsert.h"      // subCacheApiSubscriptionInsert
#include "orionld/subCache/subCacheItemAdd.h"                  // subCacheItemAdd
#include "orionld/http/httpHeaderLocationAdd.h"                // httpHeaderLocationAdd
#include "orionld/http/httpRequestHeaderAdd.h"                 // httpRequestHeaderAdd
#include "orionld/legacyDriver/legacyPostSubscriptions.h"      // legacyPostSubscriptions
#include "orionld/kjTree/kjTreeLog.h"                          // KT_TREE
#include "orionld/dbModel/dbModelFromApiSubscription.h"        // dbModelFromApiSubscription
#include "orionld/mongoc/mongocSubscriptionExists.h"           // mongocSubscriptionExists
#include "orionld/mongoc/mongocSubscriptionInsert.h"           // mongocSubscriptionInsert
#include "orionld/pernot/pernotSubCacheAdd.h"                  // pernotSubCacheAdd
#include "orionld/pernot/pernotItemRelease.h"                  // pernotItemRelease
#include "orionld/pernot/pernotSubCacheLookup.h"               // pernotSubCacheLookup
#include "orionld/mqtt/mqttParse.h"                            // mqttParse
#include "orionld/mqtt/mqttConnectionEstablish.h"              // mqttConnectionEstablish
#include "orionld/mqtt/mqttDisconnect.h"                       // mqttDisconnect
#include "orionld/q/qRender.h"                                 // qRender
#include "orionld/q/qRelease.h"                                // qRelease
#include "orionld/q/qAliasCompact.h"                           // qAliasCompact
#include "orionld/q/qPresent.h"                                // qPresent
#include "orionld/payloadCheck/pCheckSubscription.h"           // pCheckSubscription
#include "orionld/http/httpRequest.h"                          // httpRequest
#include "orionld/common/tenantList.h"                         // tenant0
#include "orionld/regMatch/regMatchSubscription.h"             // regMatchSubscription
#include "orionld/serviceRoutines/orionldPostSubscriptions.h"  // Own Interface



// -----------------------------------------------------------------------------
//
// subordinateCreate - create a subordinate subscription on another endpoint
//
// {
//   "id": "cSubP->subId:000x",
//   "type": "Subscription",
//   "entities": [
//     {
//       "type": "xxx"
//     }
//   ],
//   "notification": {
//     "endpoint": {
//       "uri": <main subscription broker IP:port + "/ngsi-ld/v1/notifications/$MAIN_SUBSCRIPTION_ID">
//     }
//   }
// }
//
SubordinateSubscription* subordinateCreate(CachedSubscription* cSubP, RegCacheItem* rciP, KjNode* subP)
{
  //
  // Modify the Subscription ID for the subordinate subscription
  //
  int  runNo = 1;

  for (SubordinateSubscription* subordinateP = cSubP->subordinateP; subordinateP != NULL; subordinateP = subordinateP->next)
  {
    runNo = K_MAX(runNo, subordinateP->runNo) + 1;
  }

  //
  // Add "type": "Subscription" if not there
  //
  KjNode* typeP = kjLookup(subP, "type");

  if (typeP == NULL)
  {
    typeP = kjString(orionldState.kjsonP, "type", "Subscription");
    kjChildPrepend(subP, typeP);
  }

  //
  // Set/Add subscription id
  //
  char subSubId[64];
  snprintf(subSubId, sizeof(subSubId), "%s:%d", cSubP->subscriptionId, runNo);
  KjNode* idP = kjLookup(subP, "id");

  if (idP != NULL)
    idP->value.s = subSubId;
  else
  {
    idP = kjString(orionldState.kjsonP, "id", subSubId);
    kjChildPrepend(subP, idP);
  }

  //
  // Modify "notification::endpoint::url" to point to this broker
  //
  char notificationUrl[512];

  if (subordinateEndpoint[0] != 0)
    snprintf(notificationUrl, sizeof(notificationUrl), "%s/notifications/%s", subordinateEndpoint, cSubP->subscriptionId);
  else
    snprintf(notificationUrl, sizeof(notificationUrl), "http://%s/ngsi-ld/ex/v1/notifications/%s", localIpAndPort, cSubP->subscriptionId);

  const char* compV[] = { "notification", "endpoint", "uri", NULL };
  KjNode*     uriP    = kjNavigate(subP, compV, NULL, NULL);

  if (uriP == NULL)
    KT_RE(NULL, "No notification:endpoint:uri field in the subscription!");

  uriP->value.s = notificationUrl;


  //
  // Create the subordinate subscription in the "child" broker
  //
  HttpKeyValue  headers[3];
  int           headerIx = 0;

  bzero(&headers, sizeof(headers));

  if (orionldState.tenantP != &tenant0)
    httpRequestHeaderAdd(&headers[headerIx++], "NGSILD-Tenant", orionldState.tenantP->tenant, 0);

  KjNode*                responseBody = NULL;
  int                    tmo          = 5000;
  OrionldProblemDetails  pd;
  char                   rciIp[128];
  char                   rciUrl[512];
  char*                  colon = strchr(rciP->ipAndPort, ':');

  if (colon != NULL)
    *colon = 0;
  strncpy(rciIp, rciP->ipAndPort, sizeof(rciIp) - 1);
  if (colon != NULL)
    *colon = ':';

  if (rciP->rest == NULL)
    snprintf(rciUrl, sizeof(rciUrl) - 1, "http://%s/ngsi-ld/v1/subscriptions", rciP->ipAndPort);
  else
    snprintf(rciUrl, sizeof(rciUrl) - 1, "http://%s%s/ngsi-ld/v1/subscriptions", rciP->ipAndPort, rciP->rest);

  KT_T(KtSR, "URL for creation of subordinate subscription '%s'", rciUrl);

  httpRequestHeaderAdd(&headers[headerIx++], "Content-Type", "application/json", 0);
  int httpStatus = httpRequest(rciIp, "POST", rciUrl, subP, NULL, headers, tmo, &responseBody, &pd);
  if ((httpStatus != 201) && (httpStatus != 200))  // ftClient responds with 200 ...
    KT_RE(NULL, "Attempt to create subordinate subscription failed with a %d", httpStatus);

  SubordinateSubscription* subordinateP = (SubordinateSubscription*) calloc(1, sizeof(SubordinateSubscription));
  if (subordinateP == NULL)
    KT_X(1, "Out of memory allocating a subordinate subscription");

  subordinateP->subscriptionId = strdup(subSubId);
  if (subordinateP->subscriptionId == NULL)
    KT_X(1, "Out of memory allocating the id of a subordinate subscription");

  subordinateP->registrationId = rciP->regId;
  subordinateP->runNo          = runNo;
  subordinateP->next           = cSubP->subordinateP;

  cSubP->subordinateP          = subordinateP;
  KT_T(KtSR, "***************** Added subordinate subs to '%s' at %p", cSubP->subscriptionId, subordinateP);

  return subordinateP;
}



// ----------------------------------------------------------------------------
//
// orionldPostSubscriptions -
//
bool orionldPostSubscriptions(void)
{
  if ((experimental == false) || (orionldState.in.legacy != NULL))
    return legacyPostSubscriptions();  // this will be removed!! (after thorough testing)

  KT_TREE(orionldState.requestTree, "Father Sub 01", KtSubordinate);

  KjNode*              subP            = orionldState.requestTree;
  KjNode*              subIdP          = orionldState.payloadIdNode;
  KjNode*              endpointP       = NULL;
  KjNode*              ldqNodeP        = NULL;
  KjNode*              uriP            = NULL;
  KjNode*              notifierInfoP   = NULL;
  KjNode*              geoCoordinatesP = NULL;
  QNode*               qTree           = NULL;
  char*                qRenderedForDb  = NULL;
  bool                 mqtt            = false;
  char*                subId           = NULL;
  bool                 b               = false;
  bool                 qValidForV2     = false;
  bool                 qIsMq           = false;
  KjNode*              showChangesP    = NULL;
  KjNode*              sysAttrsP       = NULL;
  double               timeInterval    = 0;
  OrionldRenderFormat  renderFormat    = RF_NORMALIZED;
  KjNode*              clonedSubP      = kjClone(orionldState.kjsonP, orionldState.requestTree);

  b = pCheckSubscription(subP,
                         true,
                         NULL,
                         orionldState.payloadIdNode,
                         orionldState.payloadTypeNode,
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

  if (qRenderedForDb != NULL)
    KT_T(KtQ, "qRenderedForDb: '%s'", qRenderedForDb);

  if (b == false)
  {
    if (qTree != NULL)
      qRelease(qTree);

    KT_RE(false, "pCheckSubscription FAILED");
  }

  // Subscription id special treats
  char subscriptionId[80];
  if (subIdP != NULL)
  {
    subId = subIdP->value.s;

    //
    // If the subscription already exists, a "409 Conflict" is returned
    //
    char* detail = NULL;
    if ((subCacheItemLookup(orionldState.tenantP->tenant, subId)   != NULL) ||
        (pernotSubCacheLookup(subId, orionldState.tenantP->tenant) != NULL) ||
        (mongocSubscriptionExists(subId, &detail)                  == true))
    {
      if (detail == NULL)
        orionldError(OrionldAlreadyExists, "Subscription already exists", subId, 409);
      else
        orionldError(OrionldInternalError, "Database Error", detail, 500);

      if (qTree != NULL)
        qRelease(qTree);

      return false;
    }

    strncpy(subscriptionId, subId, sizeof(subscriptionId) - 1);
  }
  else
  {
    char subscriptionId[80];
    uuidGenerate(subscriptionId, sizeof(subscriptionId), "urn:ngsi-ld:subscription:");
    subIdP = kjString(orionldState.kjsonP, "id", subscriptionId);
  }

  // Add subId to the tree
  kjChildPrepend(subP, subIdP);

  KT_TREE(orionldState.requestTree, "With ID", KtSubordinate);

  // The three 'q's ... that's also dbModel
  if (ldqNodeP != NULL)
  {
    ldqNodeP->name    = (char*) "ldQ";
    ldqNodeP->value.s = qRenderedForDb;

    // We robbed the "q" for "ldQ", need to add "q" and "mq" now - for NGSIv2
    KjNode* qNode;
    KjNode* mqNode;

    if (qValidForV2 == false)
    {
      qNode  = kjString(orionldState.kjsonP, "q", "P;!P");
      mqNode = kjString(orionldState.kjsonP, "mq", "P.P;!P.P");
      kjChildAdd(subP, qNode);
      kjChildAdd(subP, mqNode);
    }
    else if (qIsMq == false)
    {
      qNode  = kjString(orionldState.kjsonP, "q", qRenderedForDb);
      kjChildAdd(subP, qNode);
    }
    else
    {
      mqNode = kjString(orionldState.kjsonP, "mq", qRenderedForDb);
      kjChildAdd(subP, mqNode);
    }
  }

  // Timestamps
  KjNode* createdAt  = kjFloat(orionldState.kjsonP, "createdAt",  orionldState.requestTime);
  KjNode* modifiedAt = kjFloat(orionldState.kjsonP, "modifiedAt", orionldState.requestTime);

  kjChildAdd(subP, createdAt);
  kjChildAdd(subP, modifiedAt);

  // Counters ...


  //
  // If MQTT, the connection to the MQTT broker needs to be established before the subscription is accepted
  //
  bool            mqttSubscription = false;
  bool            mqtts            = false;
  char*           mqttUser         = NULL;
  char*           mqttPassword     = NULL;
  char*           mqttHost         = NULL;
  unsigned short  mqttPort         = 0;
  char*           mqttTopic        = NULL;
  char*           mqttVersion      = NULL;  // NOTE, my (KZ) local mosquitto seems to only support "mqtt3.1.1"

  if (mqtt == true)
  {
    if (timeInterval != 0)
    {
      orionldError(OrionldBadRequestData, "Not Implemented", "Notifications in MQTT for Periodic Notification Subscription", 501);
      return false;
    }

    char*  detail = NULL;
    char*  uri    = kaStrdup(&orionldState.kalloc, uriP->value.s);  // Can't destroy uriP->value.s ... mqttParse is destructive!

    if (mqttParse(uri, &mqtts, &mqttUser, &mqttPassword, &mqttHost, &mqttPort, &mqttTopic, &detail) == false)
    {
      orionldError(OrionldBadRequestData, "Invalid MQTT endpoint", detail, 400);
      return false;
    }

    //
    // Get MQTT Version  from Subscription::notification::endpoint::notifierInfo Array, "key == MQTT-Version"
    // Get MQTT QoS      from Subscription::notification::endpoint::notifierInfo Array, "key == MQTT-QoS"
    //
    // Validity of the values is verified in pcheckNotifierInfo
    //
    if (notifierInfoP)
    {
      for (KjNode* kvPairP = notifierInfoP->value.firstChildP; kvPairP != NULL; kvPairP = kvPairP->next)
      {
        KjNode* keyP   = kjLookup(kvPairP, "key");
        KjNode* valueP = kjLookup(kvPairP, "value");

        if (strcmp(keyP->name, "MQTT-Version") == 0)  mqttVersion = valueP->value.s;
      }
    }

    //
    // Establish connection with MQTT broker
    //
    if (mqttConnectionEstablish(mqtts, mqttUser, mqttPassword, mqttHost, mqttPort, mqttVersion) == false)
    {
      orionldError(OrionldInternalError, "Unable to connect to MQTT server", "xxx", 500);

      if (qTree != NULL)
        qRelease(qTree);

      return false;
    }

    mqttSubscription = true;
  }

  // sub to cache - BEFORE we change the tree to be according to the DB Model (as the DB model might change some day ...)
  CachedSubscription* cSubP = NULL;
  PernotSubscription* pSubP = NULL;

  if (timeInterval == 0)
  {
    cSubP = subCacheApiSubscriptionInsert(subP,
                                          qTree,
                                          geoCoordinatesP,
                                          orionldState.contextP,
                                          orionldState.tenantP->tenant,
                                          showChangesP,
                                          sysAttrsP,
                                          renderFormat);

    //
    // ... and into the new sub cache, which clones 'subP' and compiles its own
    // matching state from the clone. Nothing consults it yet - the legacy cache
    // above is still the one in use - but it has to be kept current from here on,
    // or it would only ever know the subscriptions that existed at startup.
    //
    subCacheItemAdd(orionldState.tenantP->subCache, subscriptionId, subP, false, orionldState.contextP);
  }
  else
  {
    // Add subscription to the pernot-cache
    KT_T(KtPernot, "qRenderedForDb: '%s'", qRenderedForDb);
    if (qTree != NULL)
      qPresent(qTree, "Pernot", "Q for pernot subscription", KtPernot);
    pSubP = pernotSubCacheAdd(subscriptionId,
                              subP,
                              endpointP,
                              qTree,
                              geoCoordinatesP,
                              orionldState.contextP,
                              orionldState.tenantP,
                              showChangesP,
                              sysAttrsP,
                              renderFormat,
                              timeInterval);

    // Signal that there's a new Pernot subscription in the cache
    // ++pernotSubCache.newSubs;
    // KT_T(KtPernotLoop, "pernotSubCache.newSubs == %d", pernotSubCache.newSubs);
  }


  //
  // Any subordinate subscriptions needed?
  //
  KT_T(KtSubordinate, "Any subordinate subscriptions needed?");
  if ((distSubsEnabled == true) && (orionldState.uriParams.local == false))
  {
    KT_T(KtSubordinate, "At least, subordinate subscriptions are ON - checking regs");

    //
    // Find matching regs
    // Create a subordinate subscription in brokers behind matching regs, if "subCreate" is in "operations"
    //
    for (RegCacheItem* rciP = orionldState.tenantP->regCache->regList; rciP != NULL; rciP = rciP->next)
    {
      char* entityTypeP;

      KT_T(KtSubordinate, "Checking reg '%s' for match to subscription '%s'", rciP->regId, cSubP->subscriptionId);
      if (regMatchSubscription(rciP, cSubP, &entityTypeP) == true)
      {
        KT_T(KtSubordinate, "Reg '%s' is a match - creating subordinate subscription", rciP->regId);

        SubordinateSubscription* subSubP = subordinateCreate(cSubP, rciP, clonedSubP);
        if (subSubP != NULL)
        {
          // Add the subordinate to subP
          KjNode* subordinateP = kjLookup(subP, "subordinate");

          if (subordinateP == NULL)
          {
            subordinateP = kjArray(orionldState.kjsonP, "subordinate");
            kjChildAdd(subP, subordinateP);
          }

          KjNode* subSubNodeP = kjObject(orionldState.kjsonP,  NULL);  // No name - part of array
          KjNode* subIdP      = kjString(orionldState.kjsonP,  "subscriptionId", subSubP->subscriptionId);
          KjNode* regIdP      = kjString(orionldState.kjsonP,  "registrationId", subSubP->registrationId);
          KjNode* runNoP      = kjInteger(orionldState.kjsonP, "runNo",          subSubP->runNo);

          kjChildAdd(subSubNodeP, subIdP);
          kjChildAdd(subSubNodeP, regIdP);
          kjChildAdd(subSubNodeP, runNoP);

          kjChildAdd(subordinateP, subSubNodeP);
        }
        else
          KT_W("Unable to create subordinate subscription for '%s'", subscriptionId);
      }
      else
        KT_T(KtSubordinate, "Reg '%s' is not a match", rciP->regId);
    }
  }

  // dbModel
  KjNode* dbSubscriptionP = subP;
  subIdP->name = (char*) "_id";  // 'id' needs to be '_id' - mongo stuff ...
  dbModelFromApiSubscription(dbSubscriptionP, false);

  // sub to db - mongocSubscriptionInsert(subP);
  if (mongocSubscriptionInsert(dbSubscriptionP, subIdP->value.s) == false)
  {
    // orionldError is done by mongocSubscriptionInsert
    KT_E("mongocSubscriptionInsert failed");
    if (mqttSubscription == true)
      mqttDisconnect(mqtts, mqttHost, mqttPort, mqttUser, mqttPassword, mqttVersion);

    if (cSubP != NULL)
      subCacheItemRemove(cSubP);
    else
      pernotItemRelease(pSubP);

    if (qTree != NULL)
      qRelease(qTree);

    return false;
  }

  orionldState.httpStatusCode = 201;
  httpHeaderLocationAdd("/ngsi-ld/v1/subscriptions/", subIdP->value.s, orionldState.tenantP->tenant);

  return true;
}
