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
#include <string.h>                                            // strcmp

extern "C"
{
#include "kbase/kMacros.h"                                     // K_VEC_SIZE
#include "ktrace/kTrace.h"                                     // KT_*
#include "kalloc/kaStrdup.h"                                   // kaStrdup
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjChildAdd, ...
#include "kjson/kjClone.h"                                     // kjClone
#include "kjson/kjNavigate.h"                                  // kjNavigate
#include "kjson/kjChildAddOrReplace.h"                         // kjChildAddOrReplace
}

#include "orionld/types/PernotSubscription.h"                  // PernotSubscription
#include "orionld/types/SubCacheItem.h"                        // SubCacheItem
#include "orionld/types/MqttInfo.h"                            // MqttInfo
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldError.h"                       // orionldError
#include "orionld/common/traceLevels.h"                        // KTrace level
#include "orionld/payloadCheck/PCHECK.h"                       // PCHECK_URI
#include "orionld/payloadCheck/pCheckSubscription.h"           // pCheckSubscription
#include "orionld/q/qBuild.h"                                  // qBuild
#include "orionld/q/qRelease.h"                                // qRelease
#include "orionld/mongoc/mongocSubscriptionLookup.h"           // mongocSubscriptionLookup
#include "orionld/mongoc/mongocSubscriptionReplace.h"          // mongocSubscriptionReplace
#include "orionld/dbModel/dbModelToApiSubscription.h"          // dbModelToApiSubscription
#include "orionld/context/orionldContextFromUrl.h"             // orionldContextFromUrl
#include "orionld/subCache/subCacheItemLookup.h"               // subCacheItemLookup
#include "orionld/subCache/subCacheItemUpdate.h"               // subCacheItemUpdate
#include "orionld/dbModel/dbModelFromApiSubscription.h"        // dbModelFromApiSubscription
#include "orionld/mqtt/mqttConnectionEstablish.h"              // mqttConnectionEstablish
#include "orionld/mqtt/mqttDisconnect.h"                       // mqttDisconnect
#include "orionld/mqtt/mqttParse.h"                            // mqttParse
#include "orionld/pernot/pernotSubCacheLookup.h"               // pernotSubCacheLookup
#include "orionld/pernot/pernotSubCacheUpdate.h"               // pernotSubCacheUpdate
#include "orionld/serviceRoutines/orionldPatchSubscription.h"  // Own Interface



// ----------------------------------------------------------------------------
//
// okToRemove - may this Subscription member be removed by an NGSI-LD Null?
//
// Only the MANDATORY members are refused here - TS 104-175 clause 5: 'type' and
// 'notification', plus 'id', which identifies the subscription.
//
// The read-only members ('status', 'createdAt', 'modifiedAt') never get this far:
// the spec says they "shall be ignored" when provided, so pCheckSubscription drops
// them from the payload whatever their value.
//
static bool okToRemove(const char* fieldName)
{
  if ((strcmp(fieldName, "id")   == 0) || (strcmp(fieldName, "@id")   == 0))
    return false;
  else if ((strcmp(fieldName, "type") == 0) || (strcmp(fieldName, "@type") == 0))
    return false;
  else if (strcmp(fieldName, "notification") == 0)
    return false;

  return true;
}



// ----------------------------------------------------------------------------
//
// dbSubscriptionMemberRemove - remove an API member from the DB-model subscription
//
// The removal is driven from the API name, not the database name, because the two
// models do not line up: 'watchedAttributes' is stored as "conditions", the NGSI-LD
// 'q' as "ldQ" (with the NGSIv2 rendering beside it in "expression"), 'geoQ' is
// spread over four members of "expression", and so on.
//
// Anything not in this table is refused rather than silently ignored - looking up a
// name that the database model does not use would just quietly do nothing.
//
static bool dbSubscriptionMemberRemove(KjNode* dbSubP, const char* apiMember)
{
  const char* dbMember = NULL;

  if      ((strcmp(apiMember, "subscriptionName") == 0) || (strcmp(apiMember, "name") == 0))  dbMember = "name";
  else if (strcmp(apiMember, "description")       == 0)                                       dbMember = "description";
  else if (strcmp(apiMember, "entities")          == 0)                                       dbMember = "entities";
  else if (strcmp(apiMember, "watchedAttributes") == 0)                                       dbMember = "conditions";
  else if (strcmp(apiMember, "throttling")        == 0)                                       dbMember = "throttling";
  else if (strcmp(apiMember, "lang")              == 0)                                       dbMember = "lang";
  else if (strcmp(apiMember, "timeInterval")      == 0)                                       dbMember = "timeInterval";
  else if (strcmp(apiMember, "datasetId")         == 0)                                       dbMember = "datasetId";
  else if (strcmp(apiMember, "jsonldContext")     == 0)                                       dbMember = "ldContext";
  else if (strcmp(apiMember, "isActive")          == 0)                                       dbMember = "status";  // absent => "true by default"
  else if ((strcmp(apiMember, "expiresAt") == 0) || (strcmp(apiMember, "expires") == 0))      dbMember = "expiration";
  else if (strcmp(apiMember, "q") == 0)
  {
    //
    // The NGSI-LD 'q' lives in "ldQ"; "expression.q"/"expression.mq" are the NGSIv2
    // rendering of the same filter and have to go with it.
    //
    KjNode* ldqP        = kjLookup(dbSubP, "ldQ");
    KjNode* expressionP = kjLookup(dbSubP, "expression");

    if (ldqP != NULL)
      kjChildRemove(dbSubP, ldqP);

    if (expressionP != NULL)
    {
      KjNode* v2qP  = kjLookup(expressionP, "q");
      KjNode* v2mqP = kjLookup(expressionP, "mq");

      if (v2qP  != NULL) v2qP->value.s  = (char*) "";
      if (v2mqP != NULL) v2mqP->value.s = (char*) "";
    }

    return true;
  }
  else if (strcmp(apiMember, "geoQ") == 0)
  {
    //
    // "geoQ" is the four geo members of "expression". They are always present in the
    // database model, empty when not in use - so they are emptied, not removed.
    //
    KjNode* expressionP = kjLookup(dbSubP, "expression");

    if (expressionP != NULL)
    {
      const char* geoFields[] = { "geometry", "coords", "georel", "geoproperty" };

      for (unsigned int ix = 0; ix < K_VEC_SIZE(geoFields); ix++)
      {
        KjNode* nodeP = kjLookup(expressionP, geoFields[ix]);

        if (nodeP != NULL)
        {
          nodeP->type    = KjString;
          nodeP->value.s = (char*) "";
        }
      }
    }

    return true;
  }
  else
  {
    orionldError(OrionldBadRequestData, "Invalid Subscription Fragment - this member cannot be removed", apiMember, 400);
    return false;
  }

  KjNode* toRemove = kjLookup(dbSubP, dbMember);

  if (toRemove != NULL)
    kjChildRemove(dbSubP, toRemove);

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
static bool ngsildSubscriptionPatch(KjNode* dbSubscriptionP, KjNode* patchTree, KjNode* qP, KjNode* expressionP, char* qRenderedForDb)
{
  KjNode* fragmentP = patchTree->value.firstChildP;
  KjNode* next;

  while (fragmentP != NULL)
  {
    next = fragmentP->next;

    KT_T(KtSR, "Patching subscription fragment '%s' for DB", fragmentP->name);

    if (fragmentP->type == KjNull)
    {
      //
      // The NGSI-LD Null - remove the member (clause 8.4.2).
      //
      // The check comes BEFORE the removal, and not - as it used to - only once the
      // member has been found in the database tree: the database model uses different
      // names, so "is it there?" answers nothing about "may it go?".
      //
      if (okToRemove(fragmentP->name) == false)
      {
        orionldError(OrionldBadRequestData, "Invalid Subscription Fragment - attempt to remove a mandatory field", fragmentP->name, 400);
        return false;
      }

      if (dbSubscriptionMemberRemove(dbSubscriptionP, fragmentP->name) == false)
        return false;  // dbSubscriptionMemberRemove calls orionldError
    }
    else
    {
      if ((fragmentP != qP) && (fragmentP != expressionP))
        kjChildAddOrReplace(dbSubscriptionP, fragmentP->name, fragmentP);
    }

    fragmentP = next;
  }


  if (qRenderedForDb != NULL)
  {
    KjNode* ldQNode = kjLookup(dbSubscriptionP, "ldQ");

    if (ldQNode == NULL)  // Add
    {
      ldQNode = kjString(orionldState.kjsonP, "ldQ", qRenderedForDb);
      kjChildAdd(dbSubscriptionP, ldQNode);
    }
    else
      ldQNode->value.s = qRenderedForDb;
  }

  //
  // If geoqP/expressionP != NULL, then it replaces the "expression" in the DB
  // If also qP != NULL, then this qP is added to geoqP/expressionP
  // If not, we have to lookup 'q' in the old "expression" and add it to geoqP/expressionP
  //
  if (expressionP != NULL)  // expressionP points to the geoQ of the PATCH payload body
  {
    // For each field in expressionP, replace in dbExpressionP
    KjNode* dbExpressionP = kjLookup(dbSubscriptionP, "expression");
    KjNode* geoqNodeP     = expressionP->value.firstChildP;
    KjNode* next;

    while (geoqNodeP != NULL)
    {
      next = geoqNodeP->next;

      KT_T(KtSR, "Checking field '%s'", geoqNodeP->name);
      // 1. Remove the node from patch payload body
      kjChildRemove(expressionP, geoqNodeP);

      // 2. Lookup the node in the DB subscription and remove it if found
      KjNode* dbGeoqNodeP = kjLookup(dbExpressionP, geoqNodeP->name);
      if (dbGeoqNodeP != NULL)
      {
        KT_T(KtSR, "Found '%s' in DB Expression - removing it from there", geoqNodeP->name);
        kjChildRemove(dbExpressionP, dbGeoqNodeP);
      }
      else
        KT_T(KtSR, "Did not find '%s' in DB Expression", geoqNodeP->name);

      // 3, Add the new geoqNodeP to dbExpressionP
      KT_T(KtSR, "Adding '%s' to DB Expression", geoqNodeP->name);
      kjChildAdd(dbExpressionP, geoqNodeP);

      geoqNodeP = next;
    }
  }
  else if (qP != NULL)
  {
    KjNode* dbExpressionP = kjLookup(dbSubscriptionP, "expression");

    if (qRenderedForDb != NULL)
      qP->value.s = qRenderedForDb;

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
static void fixDbSubscription(KjNode* dbSubscriptionP, char* qRenderedForDb)
{
  KjNode* nodeP;

  if (qRenderedForDb != NULL)
  {
    //
    // Enter "expression and fix "q"
    //
    KjNode* expressionP = kjLookup(dbSubscriptionP, "expression");
    if (expressionP != NULL)
    {
      KjNode* qP = kjLookup(expressionP, "q");
      if (qP != NULL)
        qP->value.s = qRenderedForDb;
      else
      {
        qP = kjString(orionldState.kjsonP, "q", qRenderedForDb);
        kjChildAdd(expressionP, qP);
      }
    }

    //
    // Fix "ldQ"
    //
    KjNode* ldqP = kjLookup(dbSubscriptionP, "ldQ");
    if (ldqP != NULL)
      ldqP->value.s = qRenderedForDb;
    else
    {
      ldqP = kjString(orionldState.kjsonP, "ldQ", qRenderedForDb);
      kjChildAdd(dbSubscriptionP, ldqP);
    }
  }

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
// mqttInfoFromDbTree -
//
static bool mqttInfoFromDbTree(KjNode* dbSubscriptionP, KjNode* uriP, MqttInfo* miP)
{
  char* uri = kaStrdup(&orionldState.kalloc, uriP->value.s);
  char* detail     = NULL;
  char* usernameP  = NULL;
  char* passwordP  = NULL;
  char* hostP      = NULL;
  char* topicP     = NULL;

  if (mqttParse(uri, &miP->mqtts, &usernameP, &passwordP, &hostP, &miP->port, &topicP, &detail) == false)
  {
    orionldError(OrionldBadRequestData, "Invalid MQTT endpoint", detail, 400);
    return false;
  }

  if (usernameP != NULL) strncpy(miP->username, usernameP, sizeof(miP->username) - 1);
  if (passwordP != NULL) strncpy(miP->password, passwordP, sizeof(miP->password) - 1);
  if (hostP     != NULL) strncpy(miP->host,     hostP,     sizeof(miP->host)     - 1);
  if (topicP    != NULL) strncpy(miP->topic,    topicP,    sizeof(miP->topic)    - 1);

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

  if (b == false)
    KT_RE(false, "Unable to connect to MQTT broker %s:%d", miP->host, miP->port);

  return true;
}



// ----------------------------------------------------------------------------
//
// mqttDisconnectFromInfo -
//
static void mqttDisconnectFromInfo(MqttInfo* miP)
{
  mqttDisconnect(miP->mqtts, miP->host, miP->port, miP->username, miP->password, miP->version);
}



// ----------------------------------------------------------------------------
//
// orionldPatchSubscription -
//
// 1. Check that orionldState.wildcard[0] is a valid subscription ID (a URI) - 400 Bad Request ?
// 2. Make sure the payload data is a correct Subscription fragment
//    - No values can be NULL
//    - Expand attribute names ans entity types if present
// 3. GET the subscription from mongo, by calling mongocSubscriptionLookup(orionldState.wildcard[0])
// 4. If not found - 404
//
// 5. Go over the fragment (incoming payload data) and modify the 'subscription from mongo':
//    * If the provided Fragment (merge patch) contains members that do not appear within the target (their URIs do
//      not match), those members are added to the target.
//    * the target member value is replaced by value given in the Fragment, if non-null values.
//    * If null values in the Fragment, then remove in the target
//
// 6. Call mongocSubscriptionReplace(char* subscriptionId, KjNode* subscriptionTree) to replace the old sub with the new
//    Or, dbSubscriptionUpdate(char* subscriptionId, KjNode* toAddP, KjNode* toRemoveP, KjNode* toUpdate)
//
// -----------------------------------------------------------------------------
//
// cachedSubscriptionRefresh - refresh the cached item after a PATCH
//
// The patched DB-model tree is run through the very same dbModelToApiSubscription
// that populates the cache at startup, so the cached tree can not drift in shape
// from the one built there. It is handed a clone, as that function rewrites the
// tree it is given.
//
// This is ALL the cache maintenance a PATCH needs - a removal included: the item is
// rebuilt from the patched subscription, so there is nothing to undo member by member.
//
static void cachedSubscriptionRefresh(SubCacheItem* sciP, const char* subscriptionId, KjNode* dbSubscriptionP)
{
  QNode*               qNodeP       = NULL;
  KjNode*              coordinatesP = NULL;
  KjNode*              contextNodeP = NULL;
  KjNode*              showChangesP = NULL;
  KjNode*              sysAttrsP    = NULL;
  OrionldRenderFormat  renderFormat = RF_NORMALIZED;
  double               timeInterval = 0;

  KjNode* apiSubP = dbModelToApiSubscription(kjClone(orionldState.kjsonP, dbSubscriptionP),
                                             orionldState.tenantP->tenant,
                                             true,
                                             &qNodeP,
                                             &coordinatesP,
                                             &contextNodeP,
                                             &showChangesP,
                                             &sysAttrsP,
                                             &renderFormat,
                                             &timeInterval);
  if (apiSubP == NULL)
    KT_RVE("Sub '%s': unable to refresh the new sub cache after a PATCH", subscriptionId);

  //
  // A PATCH can change "jsonldContext". It was downloaded and validated earlier in
  // this same request, so this resolves from the context cache.
  //
  OrionldContext* jsonldContextP     = NULL;
  KjNode*         jsonldContextNodeP = kjLookup(apiSubP, "jsonldContext");

  if (jsonldContextNodeP != NULL)
    jsonldContextP = orionldContextFromUrl(jsonldContextNodeP->value.s, NULL);

  subCacheItemUpdate(sciP, apiSubP, jsonldContextP);
}



bool orionldPatchSubscription(void)
{
  PCHECK_URI(orionldState.wildcard[0], true, 0, "Subscription ID must be a valid URI", orionldState.wildcard[0], 400);

  char*                subscriptionId         = orionldState.wildcard[0];
  KjNode*              qP                     = NULL;
  KjNode*              geoqP                  = kjLookup(orionldState.requestTree, "geoQ");
  KjNode*              geoCoordinatesP        = NULL;
  bool                 mqttChange             = false;
  KjNode*              subTree                = orionldState.requestTree;
  KjNode*              idNode                 = orionldState.payloadIdNode;
  KjNode*              typeNode               = orionldState.payloadTypeNode;
  QNode*               qNodeP                 = NULL;
  char*                qRenderedForDb         = NULL;
  bool                 qValidForV2            = false;
  bool                 qIsMq                  = false;
  KjNode*              uriP                   = NULL;
  KjNode*              notifierInfoP          = NULL;
  KjNode*              showChangesP           = NULL;
  KjNode*              sysAttrsP              = NULL;
  double               timeInterval           = 0;
  OrionldRenderFormat  renderFormat           = RF_NORMALIZED;
  bool                 r;

  r = pCheckSubscription(subTree,
                         false,
                         subscriptionId,
                         idNode,
                         typeNode,
                         NULL,
                         &qP,
                         &qNodeP,
                         &qRenderedForDb,
                         &qValidForV2,
                         &qIsMq,
                         &uriP,
                         &notifierInfoP,
                         &geoCoordinatesP,
                         &mqttChange,
                         &showChangesP,
                         &sysAttrsP,
                         &timeInterval,
                         &renderFormat);
  if (r == false)
  {
    if (qNodeP != NULL)
      qRelease(qNodeP);
    KT_E("pCheckSubscription FAILED");
    return false;
  }

  if (qRenderedForDb != NULL)
    KT_T(KtSR, "qRenderedForDb: '%s'", qRenderedForDb);

  //
  // 'geoqP' was picked up before pCheckSubscription, when an NGSI-LD Null was still a
  // String. pCheckSubscription has turned it into a JSON Null since - and a Null has
  // no children, so everything downstream that walks "geoQ" as an object has to be
  // told there is no geoQ here. The removal itself is done by the KjNull branch of
  // ngsildSubscriptionPatch.
  //
  if ((geoqP != NULL) && (geoqP->type == KjNull))
    geoqP = NULL;

  KjNode* dbSubscriptionP = mongocSubscriptionLookup(subscriptionId);

  if (dbSubscriptionP == NULL)
  {
    if (qNodeP != NULL)
      qRelease(qNodeP);
    orionldError(OrionldResourceNotFound, "Subscription not found", subscriptionId, 404);
    return false;
  }

  KjNode* dbTimeIntervalP = kjLookup(dbSubscriptionP, "timeInterval");
  double  dbTimeInterval  = 0;

  if (dbTimeIntervalP != NULL)
    dbTimeInterval = (dbTimeIntervalP->type == KjInt)? dbTimeIntervalP->value.i : dbTimeIntervalP->value.f;

  bool    subWasPernot    = (dbTimeInterval != 0);

  //
  // If the subscription used to be "on-change", timeInterval cannot be set
  //
  if (subWasPernot == false)
  {
    if (timeInterval != 0)
    {
      if (qNodeP != NULL)
        qRelease(qNodeP);
      orionldError(OrionldBadRequestData, "Invalid modification (on-change to timeInterval subscription)", subscriptionId, 400);
      return false;
    }
  }
  else  // If the subscription used to be "pernot", watchedAttributes+throttling cannot be set
  {
    if (timeInterval == 0)
    {
      // timeInterval was explicitly set to zero by pCheckSubscription - that means the PATCH body
      // did NOT contain timeInterval (setting to 0 is rejected by pCheckSubscription).
      // That's fine - we keep the existing timeInterval from DB.
      // However, we need to set timeInterval from the DB for the cache update later.
      timeInterval = dbTimeInterval;
    }

    KjNode* watchedAttributesP = kjLookup(subTree, "watchedAttributes");
    KjNode* throttlingP        = kjLookup(subTree, "throttling");

    if (watchedAttributesP != NULL)
    {
      if (qNodeP != NULL)
        qRelease(qNodeP);
      orionldError(OrionldBadRequestData, "Invalid modification", "pernot subscription cannot have watchedAttributes", 400);
      return false;
    }
    if (throttlingP != NULL)
    {
      if (qNodeP != NULL)
        qRelease(qNodeP);
      orionldError(OrionldBadRequestData, "Invalid modification", "pernot subscription cannot have throttling", 400);
      return false;
    }

    //
    // pCheckSubscription builds the QNode tree with pernot=false when timeInterval is not in the PATCH body.
    // For pernot subscriptions the QNode tree must be built with pernot=true so that attribute names
    // are in DB format (dotForEq, .value appended) for mongocEntitiesQuery2.
    //
    if (qNodeP != NULL)
    {
      qRelease(qNodeP);
      qNodeP = qBuild(qP->value.s, &qRenderedForDb, &qValidForV2, &qIsMq, true, true);
    }
  }


  //
  // If the subscription used to be an MQTT subscription, the MQTT connection might need closing
  // Only if MQTT data is modified though (or it stops being an MQTT subscription)
  //
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
  fixDbSubscription(dbSubscriptionP, qRenderedForDb);
  KjNode* patchBody = kjClone(orionldState.kjsonP, orionldState.requestTree);

  dbModelFromApiSubscription(orionldState.requestTree, true);

  //
  // After calling dbModelFromApiSubscription, the incoming payload data has beed structured just as the
  // API v1 database model and the original tree (obtained by calling mongocSubscriptionLookup()) can easily be
  // modified.
  // ngsildSubscriptionPatch() performs that modification.
  //
  SubCacheItem*        sciP  = NULL;
  PernotSubscription*  pSubP = NULL;

  if (subWasPernot == false)
  {
    sciP = subCacheItemLookup(orionldState.tenantP->subCache, subscriptionId);
    if (sciP == NULL)
    {
      orionldError(OrionldResourceNotFound, "Subscription not found", subscriptionId, 404);
      return false;
    }
  }
  else
  {
    pSubP = pernotSubCacheLookup(orionldState.tenantP->tenant, subscriptionId);
    if (pSubP == NULL)
    {
      orionldError(OrionldResourceNotFound, "Subscription not found", subscriptionId, 404);
      return false;
    }
  }

  if (ngsildSubscriptionPatch(dbSubscriptionP, orionldState.requestTree, qP, geoqP, qRenderedForDb) == false)
  {
    if (qNodeP != NULL)
      qRelease(qNodeP);
    KT_RE(false, "ngsildSubscriptionPatch failed!");
  }

  //
  // Update modifiedAt
  //
  KjNode* modifiedAtP = kjLookup(dbSubscriptionP, "modifiedAt");

  if (modifiedAtP != NULL)
  {
    KT_T(KtSR, "%s: Old modifiedAt: %f", subscriptionId, modifiedAtP->value.f);
    modifiedAtP->value.f = orionldState.requestTime;
  }
  else
  {
    modifiedAtP = kjFloat(orionldState.kjsonP, "modifiedAt", orionldState.requestTime);
    kjChildAdd(dbSubscriptionP, modifiedAtP);
  }
  KT_T(KtSR, "%s: New modifiedAt: %f", subscriptionId, orionldState.requestTime);

  // Connect to MQTT broker, if needed
  if ((newIsMqtt == true) && (mqttChange == true))
  {
    // GET mix of new and old MQTT info from patched dbSubscriptionP
    bzero(&newMqttInfo, sizeof(newMqttInfo));
    mqttInfoFromDbTree(dbSubscriptionP, newUriP, &newMqttInfo);
    if (mqttConnectFromInfo(&newMqttInfo) == false)
    {
      if (qNodeP != NULL)
        qRelease(qNodeP);
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
  // If jsonldContext was explicitly patched, update the ldContext in the DB subscription
  // (dbModelFromApiSubscription sets ldContext from orionldState.contextP which is the @context
  // of the PATCH request, not the jsonldContext field in the subscription body)
  //
  KjNode* patchedJsonldContextP = kjLookup(patchBody, "jsonldContext");
  if (patchedJsonldContextP != NULL)
  {
    KjNode* dbLdContextP = kjLookup(dbSubscriptionP, "ldContext");
    if (dbLdContextP != NULL)
      dbLdContextP->value.s = patchedJsonldContextP->value.s;
  }

  //
  // Overwrite the current Subscription in the database
  //
  if (mongocSubscriptionReplace(subscriptionId, dbSubscriptionP) == false)
  {
    if (qNodeP != NULL)
      qRelease(qNodeP);
    orionldError(OrionldInternalError, "Database Error", "patching a subscription", 500);
    return false;
  }

  // Modify the subscription in the subscription cache
  if (subWasPernot == false)
  {
    //
    // The cached item is rebuilt from the patched DB tree, so the QNode that
    // pCheckSubscription built for this request has no owner - the cache compiles
    // its own from "ldQ".
    //
    if (qNodeP != NULL)
      qRelease(qNodeP);

    cachedSubscriptionRefresh(sciP, subscriptionId, dbSubscriptionP);
  }
  else
  {
    if (pernotSubCacheUpdate(pSubP, patchBody, qNodeP, geoCoordinatesP, timeInterval) == false)
      KT_E("Internal Error (unable to update the cached pernot subscription '%s' after a PATCH)", subscriptionId);
  }

  // All OK? 204 No Content
  orionldState.httpStatusCode = 204;

  return true;
}
