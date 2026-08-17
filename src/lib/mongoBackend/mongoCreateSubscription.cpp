/*
*
* Copyright 2016 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Fermin Galan
*/
#include <string>
#include <vector>
#include <map>

#include "mongo/client/dbclient.h"

#include "orionld/types/OrionldTenant.h"                           // OrionldTenant

#include "common/defaultValues.h"
#include "apiTypesV2/Subscription.h"
#include "rest/OrionError.h"
#include "orionld/common/orionldState.h"             // orionldState

#include "mongoBackend/connectionOperations.h"
#include "mongoBackend/MongoGlobal.h"
#include "mongoBackend/MongoCommonSubscription.h"
#include "mongoBackend/dbConstants.h"
#include "orionld/mongoc/mongocSubCountersUpdate.h"          // mongocSubCountersUpdate
#include "orionld/q/qBuild.h"                                // qBuild
#include "orionld/q/qRelease.h"                              // qRelease
#include "orionld/subCache/subCacheItemFromDb.h"             // subCacheItemFromDb (the new sub cache)
#include "orionld/subCache/subCacheItemLookup.h"             // subCacheItemLookup
#include "orionld/types/SubCacheItem.h"                      // SubCacheItem
#include "mongoBackend/mongoCreateSubscription.h"



/* ****************************************************************************
*
* USING
*/
using mongo::BSONObj;
using mongo::BSONObjBuilder;
using ngsiv2::Subscription;



// -----------------------------------------------------------------------------
//
// setTimestamp -
//
static void setTimestamp(const char* name, double ts, mongo::BSONObjBuilder* bobP)
{
  bobP->append(name, ts);
}



/* ****************************************************************************
*
* mongoCreateSubscription -
*
* Returns:
* - subId: subscription successfully created ('oe' must be ignored), the subId
*   must be used to fill Location header
* - "": subscription creation fail (look at 'oe')
*/
std::string mongoCreateSubscription
(
  const Subscription&              sub,
  OrionError*                      oe,
  OrionldTenant*                   tenantP,
  const std::vector<std::string>&  servicePathV,
  const char*                      xauthToken,
  const std::string&               fiwareCorrelator,
  const std::string&               ldContext,
  const std::string&               lang
)
{
  bool reqSemTaken = false;

  reqSemTake(__FUNCTION__, "ngsiv2 create subscription request", SemWriteOp, &reqSemTaken);

  BSONObjBuilder     b;
  std::string        servicePath      = servicePathV[0] == "" ? SERVICE_PATH_ALL : servicePathV[0];
  bool               notificationDone = false;
#ifdef ORIONLD
  const std::string  subId            = setSubscriptionId(sub, &b);
#else
  const std::string  subId            = setNewSubscriptionId(&b);
#endif

  // Build the BSON object to insert
  setExpiration(sub, &b);
  setHttpInfo(sub, &b);
  setThrottling(sub, &b);
  setServicePath(servicePath.c_str(), &b);
  setDescription(sub, &b);
  setStatus(sub, &b);
  setEntities(sub, &b);
  setAttrs(sub, &b);
  setMetadata(sub, &b);
  setBlacklist(sub, &b);

  double now = orionldState.requestTime;
  if (sub.name      != "")  setName(sub, &b);
  if (sub.ldContext != "")  setContext(sub, &b);
  if (sub.lang      != "")  setLang(sub, &b);
  if (sub.csf       != "")  setCsf(sub, &b);

  setTimestamp("createdAt",  now, &b);
  setTimestamp("modifiedAt", now, &b);

  // ---------------------------------------------------------------------------
  //
  // setTimeInterval
  //
  // setTimeInterval is not called as this tiny little call causes thousands of errors in valgrind.
  //
  // Orion-LD doesn't support periodic notifications anyway, so the value is not used.
  // Once (if) we decide that Orion-LD is to implement periodic notifications, the problems will have to be fixed.
  // The field "timeInterval" in Subscription is to be changed from 'int' to 'double' and wherever the field is used we
  // need to adapt the code to 'timeInterval' now being a 'double' and not an 'int'
  //
  // setTimeInterval(sub, &b);


  std::string status = sub.status == ""?  STATUS_ACTIVE : sub.status;

  //
  // An NGSI-LD 'q' is parsed here, BEFORE anything is written to the database.
  //
  // This is the legacy create path, which does not go through pCheckSubscription,
  // so nothing else validates the filter. It used to be validated as a side effect
  // of filling the old subscription cache - qBuild was called while building the
  // cached item, and a failure there aborted the whole create.
  //
  // qBuild reports the error itself (orionldError), so all that is needed here is
  // to give up before the subscription reaches the database.
  //
  if ((orionldState.apiVersion == API_VERSION_NGSILD_V1) && (sub.subject.condition.expression.q != ""))
  {
    char*  qText      = NULL;
    bool   validForV2 = true;
    bool   isMq       = false;
    QNode* qP         = qBuild(sub.subject.condition.expression.q.c_str(), &qText, &validForV2, &isMq, true, false);

    if (qP != NULL)
      qRelease(qP);

    if (qText == NULL)
      return "";
  }

  //
  // The CONDITIONS only - no notification yet.
  //
  // The initial notification is sent further down, once the subscription is in the
  // database AND in both caches. Sending it from here, as this used to, means it can
  // finish before the subscription cache has an item to record the outcome in - and
  // then whether that first notification succeeded or failed is simply lost.
  //
  setCondsAndInitialNotify(sub,
                           subId,
                           status,
                           sub.notification.attributes,
                           sub.notification.metadata,
                           sub.notification.httpInfo,
                           sub.notification.blacklist,
                           sub.attrsFormat,
                           tenantP,
                           servicePathV,
                           xauthToken,
                           fiwareCorrelator,
                           &b,
                           &notificationDone,
                           false);  // notify

  setExpression(sub, &b);
  setFormat(sub, &b);

  BSONObj doc = b.obj();

  // Insert in DB
  std::string err;

  if (!collectionInsert(tenantP->subscriptions, doc, &err))
  {
    reqSemGive(__FUNCTION__, "ngsiv2 create subscription request", reqSemTaken);
    oe->fill(SccReceiverInternalError, err);

    return "";
  }

  reqSemGive(__FUNCTION__, "ngsiv2 create subscription request", reqSemTaken);

  //
  // ... and into the new subscription cache, by reading the subscription back and
  // running it through the same dbModelToApiSubscription the startup loader uses.
  // See subCacheItemFromDb.
  //
  subCacheItemFromDb(tenantP, subId.c_str());

  //
  // The cross-API render formats ("x-ngsiv2-normalized", ...) do NOT survive the
  // database - it stores plain "normalized" for all of them - so the item that
  // was just built from the database has lost that distinction. The request still
  // has it, and the request is the authority, so it is put back.
  //
  // (The old sub-cache never noticed: it was filled from the request, not from the
  // database. It lost the distinction too, but only on a broker restart.)
  //
  SubCacheItem* sciP = subCacheItemLookup(tenantP->subCache, subId.c_str());

  if (sciP != NULL)
    sciP->renderFormat = sub.attrsFormat;

  //
  // ... and NOW the initial notification.
  //
  // Everything it needs is in place: the subscription is in the database, in the old
  // cache and in the new one - so whatever the notification thread has to say about
  // how it went, there is an item to say it to.
  //
  setCondsAndInitialNotify(sub,
                           subId,
                           status,
                           sub.notification.attributes,
                           sub.notification.metadata,
                           sub.notification.httpInfo,
                           sub.notification.blacklist,
                           sub.attrsFormat,
                           tenantP,
                           servicePathV,
                           xauthToken,
                           fiwareCorrelator,
                           NULL,  // the conditions are already stored - only notify
                           &notificationDone,
                           true);  // notify

  //
  // The counters of that first notification. They used to be part of the document
  // being inserted, which is no longer possible - the document is written before the
  // notification is sent. So they are written on top, exactly as every later
  // notification's counters are.
  //
  if (notificationDone == true)
    mongocSubCountersUpdate(tenantP, subId.c_str(), (sub.ldContext != ""), 1, 0, 0, orionldState.requestTime, -1, -1, false);

  return subId;
}
