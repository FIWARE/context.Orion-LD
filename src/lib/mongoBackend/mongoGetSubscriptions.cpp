/*
*
* Copyright 2015 Telefonica Investigacion y Desarrollo, S.A.U
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
* Author: Orion dev team
*/
#include <string>
#include <vector>
#include <map>

#include "mongo/client/dbclient.h"

#include "logMsg/logMsg.h"
#include "logMsg/traceLevels.h"

#include "orionld/types/OrionldTenant.h"
#include "common/sem.h"
#include "common/statistics.h"
#include "common/idCheck.h"
#include "common/errorMessages.h"
#include "rest/ConnectionInfo.h"
#include "cache/subCache.h"
#include "apiTypesV2/Subscription.h"
#include "orionld/common/orionldState.h"             // orionldState
#include "mongoBackend/MongoGlobal.h"
#include "mongoBackend/MongoCommonSubscription.h"
#include "mongoBackend/connectionOperations.h"
#include "mongoBackend/safeMongo.h"
#include "mongoBackend/dbConstants.h"
#include "mongoBackend/mongoGetSubscriptions.h"



// -----------------------------------------------------------------------------
//
// FIXME: move mongoSetLdTimestamp from mongoLdRegistrationAux to a more neutral aux file
//
extern void mongoSetLdTimestamp(double* timestampP, const char* name, const mongo::BSONObj& bobj);



/* ****************************************************************************
*
* USING -
*/
using mongo::BSONObj;
using mongo::BSONElement;
using mongo::DBClientCursor;
using mongo::DBClientBase;
using mongo::Query;
using mongo::OID;
using ngsiv2::Subscription;
using ngsiv2::EntID;



/* ****************************************************************************
*
* setSubscriptionId -
*/
static void setNewSubscriptionId(Subscription* s, const BSONObj* rP)
{
  s->id = getFieldF(rP, "_id").OID().toString();
}



/* ****************************************************************************
*
* setDescription -
*/
static void setDescription(Subscription* s, const BSONObj* bobjP)
{
  s->description = bobjP->hasField(CSUB_DESCRIPTION) ? getStringFieldF(bobjP, CSUB_DESCRIPTION) : "";
}



/* ****************************************************************************
*
* setSubject -
*/
static void setSubject(Subscription* s, const BSONObj* rP)
{
  // Entities
  std::vector<BSONElement> ents = getFieldF(rP, CSUB_ENTITIES).Array();
  for (unsigned int ix = 0; ix < ents.size(); ++ix)
  {
    BSONObj ent               = ents[ix].embeddedObject();
    std::string id            = getStringFieldF(&ent, CSUB_ENTITY_ID);
    std::string type          = ent.hasField(CSUB_ENTITY_TYPE)? getStringFieldF(&ent, CSUB_ENTITY_TYPE) : "";
    std::string isPattern     = getStringFieldF(&ent, CSUB_ENTITY_ISPATTERN);
    bool        isTypePattern = ent.hasField(CSUB_ENTITY_ISTYPEPATTERN)? getBoolFieldF(&ent, CSUB_ENTITY_ISTYPEPATTERN) : false;

    EntID en;
    if (isFalse(isPattern))
    {
      en.id = id;
    }
    else
    {
      en.idPattern = id;
    }

    if (!isTypePattern)
    {
      en.type = type;
    }
    else  // isTypePattern
    {
      en.typePattern = type;
    }

    s->subject.entities.push_back(en);
  }

  // Condition
  setStringVectorF(rP, CSUB_CONDITIONS, &(s->subject.condition.attributes));

  if (rP->hasField(CSUB_EXPR))
  {
    mongo::BSONObj expression;
    getObjectFieldF(&expression, rP, CSUB_EXPR);

    const char*  q           = getStringFieldF(&expression, CSUB_EXPR_Q);
    const char*  mq          = getStringFieldF(&expression, CSUB_EXPR_MQ);
    const char*  geo         = getStringFieldF(&expression, CSUB_EXPR_GEOM);
    const char*  coords      = getStringFieldF(&expression, CSUB_EXPR_COORDS);
    const char*  georel      = getStringFieldF(&expression, CSUB_EXPR_GEOREL);
    const char*  geoproperty = getStringFieldF(&expression, "geoproperty");

    if (q[0]  != 0)           s->subject.condition.expression.q           = q;
    if (mq[0] != 0)           s->subject.condition.expression.mq          = mq;
    if (geo[0] != 0)          s->subject.condition.expression.geometry    = geo;
    if (coords[0] != 0)       s->subject.condition.expression.coords      = coords;
    if (georel[0] != 0)       s->subject.condition.expression.georel      = georel;
    if (geoproperty[0] != 0)  s->subject.condition.expression.geoproperty = geoproperty;
  }
}



/* ****************************************************************************
*
* setNotification -
*/
static void setNotification(Subscription* subP, const BSONObj* rP, OrionldTenant* tenantP)
{
  // Attributes
  setStringVectorF(rP, CSUB_ATTRS, &(subP->notification.attributes));

  // Metadata
  if (rP->hasField(CSUB_METADATA))
  {
    setStringVectorF(rP, CSUB_METADATA, &(subP->notification.metadata));
  }

  subP->notification.httpInfo.fill(rP);

  ngsiv2::Notification* nP = &subP->notification;

  subP->throttling      = rP->hasField(CSUB_THROTTLING)?       getNumberFieldAsDoubleF(rP, CSUB_THROTTLING)       : -1;
  nP->lastNotification  = rP->hasField(CSUB_LASTNOTIFICATION)? getNumberFieldAsDoubleF(rP, CSUB_LASTNOTIFICATION) : -1;
  nP->timesSent         = rP->hasField(CSUB_COUNT)?            getIntOrLongFieldAsLongF(rP, CSUB_COUNT)           : 0;
  nP->blacklist         = rP->hasField(CSUB_BLACKLIST)?        getBoolFieldF(rP, CSUB_BLACKLIST)                  : false;
  nP->lastFailure       = rP->hasField(CSUB_LASTFAILURE)?      getNumberFieldAsDoubleF(rP, CSUB_LASTFAILURE)      : -1;
  nP->lastSuccess       = rP->hasField(CSUB_LASTSUCCESS)?      getNumberFieldAsDoubleF(rP, CSUB_LASTSUCCESS)      : -1;

  // Attributes format
  subP->attrsFormat = rP->hasField(CSUB_FORMAT)? stringToRenderFormat(getStringFieldF(rP, CSUB_FORMAT)) : NGSI_V1_LEGACY;


  //
  // Check values from subscription cache, update object from cache-values if necessary
  //
  // NOTE: only 'lastNotificationTime' and 'count'
  //
  cacheSemTake(__FUNCTION__, "get lastNotification and count");
  CachedSubscription* cSubP = subCacheItemLookup(tenantP->tenant, subP->id.c_str());
  if (cSubP != NULL)
  {
    if (cSubP->lastNotificationTime > subP->notification.lastNotification)
    {
      subP->notification.lastNotification = cSubP->lastNotificationTime;
    }

    subP->notification.timesSent += cSubP->count;

    if (cSubP->lastFailure > subP->notification.lastFailure)
      subP->notification.lastFailure = cSubP->lastFailure;

    if (cSubP->lastSuccess > subP->notification.lastSuccess)
      subP->notification.lastSuccess = cSubP->lastSuccess;
  }

  cacheSemGive(__FUNCTION__, "get lastNotification and count");
}



/* ****************************************************************************
*
* setStatus -
*/
static void setStatus(Subscription* s, const BSONObj& r)
{
  s->expires = r.hasField(CSUB_EXPIRATION)? getNumberFieldAsDoubleF(&r, CSUB_EXPIRATION) : -1;

  //
  // Status
  // FIXME P10: use an enum for active/inactive/expired
  //
  // NOTE:
  //   if the field CSUB_EXPIRATION is not present in the subscription, then the default
  //   value of "-1 == never expires" is used.
  //
  if ((s->expires > orionldState.requestTime) || (s->expires == -1))
  {
    s->status = r.hasField(CSUB_STATUS) ? getStringFieldF(&r, CSUB_STATUS) : STATUS_ACTIVE;
  }
  else
  {
    s->status = "expired";
  }
}



#ifdef ORIONLD
/* ****************************************************************************
*
* setSubscriptionId -
*/
static void setSubscriptionId(Subscription* s, const BSONObj* rP)
{
  s->id = getStringFieldF(rP, "_id");
}



/* ****************************************************************************
*
* setName -
*/
static void setName(Subscription* subP, const BSONObj* rP)
{
  if (rP->hasField(CSUB_NAME))
    subP->name = getStringFieldF(rP, CSUB_NAME);
}



/* ****************************************************************************
*
* extractContext -
*/
static void extractContext(Subscription* subP, const BSONObj* rP)
{
  if (rP->hasField("ldContext"))
    subP->ldContext = getStringFieldF(rP, "ldContext");
}



/* ****************************************************************************
*
* extractLang -
*/
static void extractLang(Subscription* subP, const BSONObj* rP)
{
  if (rP->hasField("lang"))
    subP->lang = getStringFieldF(rP, "lang");
}



/* ****************************************************************************
*
* setCsf -
*/
static void setCsf(Subscription* subP, const BSONObj* rP)
{
  if (rP->hasField("csf"))
    subP->csf = getStringFieldF(rP, "csf");
}



/* ****************************************************************************
*
* setLdQ -
*/
static void setLdQ(Subscription* subP, const BSONObj* rP)
{
  if (rP->hasField("ldQ"))
    subP->ldQ = getStringFieldF(rP, "ldQ");
}


/* ****************************************************************************
*
* setMimeType -
*/
static void setMimeType(Subscription* s, const BSONObj* rP)
{
  if (rP->hasField(CSUB_MIMETYPE))
  {
    const char* mimeTypeString = getStringFieldF(rP, CSUB_MIMETYPE);

    s->notification.httpInfo.mimeType = longStringToMimeType(mimeTypeString);
  }
}



/* ****************************************************************************
*
* setTimeInterval -
*/
static void setTimeInterval(Subscription* s, const BSONObj& r)
{
  s->timeInterval = (int) (r.hasField("timeInterval") ? getIntOrLongFieldAsLongF(&r, "timeInterval") : -1);
}

#endif


/* ****************************************************************************
*
* mongoListSubscriptions -
*/
void mongoListSubscriptions
(
  std::vector<Subscription>*           subs,
  OrionError*                          oe,
  OrionldTenant*                       tenantP,
  const std::string&                   servicePath,  // FIXME P4: vector of strings and not just a single string? See #3100
  int                                  limit,
  int                                  offset,
  long long*                           count
)
{
  bool  reqSemTaken = false;

  reqSemTake(__FUNCTION__, "Mongo List Subscriptions", SemReadOp, &reqSemTaken);

  LM_T(LmtMongo, ("Mongo List Subscriptions"));

  /* ONTIMEINTERVAL subscriptions are not part of NGSIv2, so they are excluded.
   * Note that expiration is not taken into account (in the future, a q= query
   * could be added to the operation in order to filter results)
   */
  std::auto_ptr<DBClientCursor>  cursor;
  std::string                    err;
  Query                          q;

  // FIXME P6: This here is a bug ... See #3099 for more info
  if (!servicePath.empty() && (servicePath != "/#"))
  {
    q = Query(BSON(CSUB_SERVICE_PATH << servicePath));
  }

  q.sort(BSON("_id" << 1));

  TIME_STAT_MONGO_READ_WAIT_START();
  DBClientBase* connection = getMongoConnection();
  if (!collectionRangedQuery(connection,
                             tenantP->subscriptions,
                             q,
                             limit,
                             offset,
                             &cursor,
                             count,
                             &err))
  {
    releaseMongoConnection(connection);
    TIME_STAT_MONGO_READ_WAIT_STOP();
    reqSemGive(__FUNCTION__, "Mongo List Subscriptions", reqSemTaken);
    *oe = OrionError(SccReceiverInternalError, err);
    return;
  }
  TIME_STAT_MONGO_READ_WAIT_STOP();

  /* Process query result */
  unsigned int docs = 0;

  while (moreSafe(cursor))
  {
    BSONObj r;

    if (!nextSafeOrErrorF(cursor, &r, &err))
    {
      LM_E(("Runtime Error (exception in nextSafe(): %s - query: %s)", err.c_str(), q.toString().c_str()));
      continue;
    }

    docs++;
    LM_T(LmtMongo, ("retrieved document [%d]: '%s'", docs, r.toString().c_str()));

    Subscription  sub;

    setSubject(&sub, &r);
    setNewSubscriptionId(&sub, &r);
    setDescription(&sub, &r);
    setStatus(&sub, r);
    setNotification(&sub, &r, tenantP);

    subs->push_back(sub);
  }

  releaseMongoConnection(connection);
  reqSemGive(__FUNCTION__, "Mongo List Subscriptions", reqSemTaken);

  *oe = OrionError(SccOk);
}



/* ****************************************************************************
*
* mongoGetSubscription -
*/
void mongoGetSubscription
(
  ngsiv2::Subscription*               subP,
  OrionError*                         oe,
  const std::string&                  idSub,
  OrionldTenant*                      tenantP
)
{
  bool         reqSemTaken = false;
  std::string  err;
  OID          oid;
  StatusCode   sc;

  SubscriptionId id(idSub);
  if (safeGetSubId(&id, &oid, &sc) == false)
  {
    *oe = OrionError(sc);
    return;
  }

  reqSemTake(__FUNCTION__, "Mongo Get Subscription", SemReadOp, &reqSemTaken);

  LM_T(LmtMongo, ("Mongo Get Subscription"));

  std::auto_ptr<DBClientCursor>  cursor;
  BSONObj                        q = BSON("_id" << oid);

  TIME_STAT_MONGO_READ_WAIT_START();
  DBClientBase* connection = getMongoConnection();
  if (!collectionQuery(connection, tenantP->subscriptions, q, &cursor, &err))
  {
    releaseMongoConnection(connection);
    TIME_STAT_MONGO_READ_WAIT_STOP();
    reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
    *oe = OrionError(SccReceiverInternalError, err);
    return;
  }
  TIME_STAT_MONGO_READ_WAIT_STOP();

  /* Process query result */
  if (moreSafe(cursor))
  {
    BSONObj r;

    if (!nextSafeOrErrorF(cursor, &r, &err))
    {
      releaseMongoConnection(connection);
      LM_E(("Runtime Error (exception in nextSafe(): %s - query: %s)", err.c_str(), q.toString().c_str()));
      reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
      *oe = OrionError(SccReceiverInternalError, std::string("exception in nextSafe(): ") + err.c_str());
      return;
    }
    LM_T(LmtMongo, ("retrieved document: '%s'", r.toString().c_str()));

    setNewSubscriptionId(subP, &r);
    setDescription(subP, &r);
    setSubject(subP, &r);
    setNotification(subP, &r, tenantP);
    setStatus(subP, r);

    if (moreSafe(cursor))
    {
      releaseMongoConnection(connection);
      // Ooops, we expect only one
      LM_T(LmtMongo, ("more than one subscription: '%s'", idSub.c_str()));
      reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
      *oe = OrionError(SccConflict);

      return;
    }
  }
  else
  {
    releaseMongoConnection(connection);
    LM_T(LmtMongo, ("subscription not found: '%s'", idSub.c_str()));
    reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
    *oe = OrionError(SccContextElementNotFound, ERROR_DESC_NOT_FOUND_SUBSCRIPTION, ERROR_NOT_FOUND);

    return;
  }

  releaseMongoConnection(connection);
  reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);

  *oe = OrionError(SccOk);
}



#ifdef ORIONLD
/* ****************************************************************************
*
* mongoGetLdSubscription -
*/
bool mongoGetLdSubscription
(
  ngsiv2::Subscription*  subP,
  const char*            subId,
  OrionldTenant*         tenantP,
  int*                   statusCodeP,
  char**                 detailsP
)
{
  bool                           reqSemTaken = false;
  std::string                    err;
  std::auto_ptr<DBClientCursor>  cursor;
  BSONObj                        q     = BSON("_id" << subId);

  reqSemTake(__FUNCTION__, "Mongo Get Subscription", SemReadOp, &reqSemTaken);

  TIME_STAT_MONGO_READ_WAIT_START();
  DBClientBase* connection = getMongoConnection();

  if (!collectionQuery(connection, tenantP->subscriptions, q, &cursor, &err))
  {
    releaseMongoConnection(connection);
    TIME_STAT_MONGO_READ_WAIT_STOP();
    reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
    *detailsP    = (char*) "Internal Error during DB-query";
    *statusCodeP = 500;
    return false;
  }
  TIME_STAT_MONGO_READ_WAIT_STOP();

  /* Process query result */
  if (moreSafe(cursor))
  {
    BSONObj r;

    if (!nextSafeOrErrorF(cursor, &r, &err))
    {
      releaseMongoConnection(connection);
      LM_E(("Runtime Error (exception in nextSafe(): %s - query: %s)", err.c_str(), q.toString().c_str()));
      reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
      *detailsP    = (char*) "Runtime Error (exception in nextSafe)";
      *statusCodeP = 500;
      return false;
    }
    LM_T(LmtMongo, ("retrieved document: '%s'", r.toString().c_str()));

    setSubscriptionId(subP, &r);
    setDescription(subP, &r);
    setMimeType(subP, &r);
    setSubject(subP, &r);
    setNotification(subP, &r, tenantP);
    setStatus(subP, r);
    setName(subP, &r);
    extractContext(subP, &r);
    extractLang(subP, &r);
    setCsf(subP, &r);
    setLdQ(subP, &r);
    setTimeInterval(subP, r);
    mongoSetLdTimestamp(&subP->createdAt, "createdAt", r);
    mongoSetLdTimestamp(&subP->modifiedAt, "modifiedAt", r);

    if (moreSafe(cursor))
    {
      releaseMongoConnection(connection);

      // Ooops, we expected only one
      LM_T(LmtMongo, ("more than one subscription: '%s'", subId));
      reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
      *detailsP    = (char*) "more than one subscription matched";
      *statusCodeP = 409;
      return false;
    }
  }
  else
  {
    releaseMongoConnection(connection);
    LM_T(LmtMongo, ("subscription not found: '%s'", subId));
    reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);
    *detailsP    = (char*) "subscription not found";
    *statusCodeP = 404;
    return false;
  }

  releaseMongoConnection(connection);
  reqSemGive(__FUNCTION__, "Mongo Get Subscription", reqSemTaken);

  *statusCodeP = 200;
  return true;
}



/* ****************************************************************************
*
* mongoGetLdSubscriptions -
*/
bool mongoGetLdSubscriptions
(
  const char*                         servicePath,
  std::vector<ngsiv2::Subscription>*  subVecP,
  OrionldTenant*                      tenantP,
  long long*                          countP,
  OrionError*                         oeP
)
{
  bool reqSemTaken = false;

  reqSemTake(__FUNCTION__, "Mongo GET Subscriptions", SemReadOp, &reqSemTaken);

  LM_T(LmtMongo, ("Mongo GET Subscriptions"));

  /* ONTIMEINTERVAL subscriptions are not part of NGSIv2, so they are excluded.
   * Note that expiration is not taken into account (in the future, a q= query
   * could be added to the operation in order to filter results)
   */
  std::auto_ptr<DBClientCursor>  cursor;
  std::string                    err;
  Query                          q;

  // FIXME P6: This here is a bug ... See #3099 for more info
  if ((servicePath != NULL) && (strcmp(servicePath, "/#") != 0))
  {
    q = Query(BSON(CSUB_SERVICE_PATH << servicePath));
  }

  q.sort(BSON("_id" << 1));

  TIME_STAT_MONGO_READ_WAIT_START();
  DBClientBase* connection = getMongoConnection();
  if (!collectionRangedQuery(connection,
                             tenantP->subscriptions,
                             q,
                             orionldState.uriParams.limit,
                             orionldState.uriParams.offset,
                             &cursor,
                             countP,
                             &err))
  {
    releaseMongoConnection(connection);
    TIME_STAT_MONGO_READ_WAIT_STOP();
    reqSemGive(__FUNCTION__, "Mongo List Subscriptions", reqSemTaken);

    oeP->code    = SccReceiverInternalError;
    oeP->details = err;
    return false;
  }
  TIME_STAT_MONGO_READ_WAIT_STOP();

  /* Process query result */
  unsigned int docs = 0;

  while (moreSafe(cursor))
  {
    BSONObj  r;

    if (!nextSafeOrErrorF(cursor, &r, &err))
    {
      LM_E(("Runtime Error (exception in nextSafe(): %s - query: %s)", err.c_str(), q.toString().c_str()));
      continue;
    }

    docs++;
    LM_T(LmtMongo, ("retrieved document [%d]: '%s'", docs, r.toString().c_str()));

    Subscription s;

    setSubscriptionId(&s, &r);
    setDescription(&s, &r);
    setMimeType(&s, &r);
    setSubject(&s, &r);
    setNotification(&s, &r, tenantP);
    setStatus(&s, r);
    setName(&s, &r);
    extractContext(&s, &r);
    extractLang(&s, &r);
    setCsf(&s, &r);
    setLdQ(&s, &r);
    setTimeInterval(&s, r);

    subVecP->push_back(s);
  }

  releaseMongoConnection(connection);
  reqSemGive(__FUNCTION__, "Mongo List Subscriptions", reqSemTaken);

  oeP->code = SccOk;
  return true;
}

#endif
