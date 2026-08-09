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
#include <string>                                                // std::string
#include <vector>                                                // std::vector

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/types/SubV2Info.h"                             // SubV2Info
#include "orionld/types/Verb.h"                                  // verbFromString, verbToString

#include "common/MimeType.h"                                     // mimeTypeFromString
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheItemV2Compile.h"              // Own interface



// -----------------------------------------------------------------------------
//
// stringArrayFill - a KjNode Array of String into a std::vector<std::string>
//
static void stringArrayFill(std::vector<std::string>* vecP, KjNode* arrayP)
{
  vecP->clear();

  if ((arrayP == NULL) || (arrayP->type != KjArray))
    return;

  for (KjNode* itemP = arrayP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type == KjString)
      vecP->push_back(itemP->value.s);
  }
}



// -----------------------------------------------------------------------------
//
// subCacheItemV2Compile - build the NGSIv2 matching state of a cached Subscription
//
// EVERY subscription gets one, not only the NGSIv2-created ones. An entity
// updated through the NGSIv2 API is matched against the subscription cache by
// subCacheMatch, and that has always included the NGSI-LD subscriptions: the
// NGSI-LD create path writes an NGSIv2 rendering of 'q'/'mq' into the database
// and a servicePath of "/#" precisely so that they can be matched that way.
// Building this only for v2 subscriptions would silently stop notifying NGSI-LD
// subscriptions whenever an entity is touched through the v2 API.
//
// NGSIv2's 'q' and NGSI-LD's 'q' are similar but NOT compatible, so the two are
// compiled separately and never mixed: this makes StringFilters out of "q"/"mq"
// (the v2 renderings), while subCacheItemCompile makes a QNode tree out of "ldQ".
//
void subCacheItemV2Compile(SubCacheItem* sciP)
{
  //
  // Thrown away and rebuilt, never patched in place: this is re-run on every
  // PATCH, and a compiled StringFilter that the new 'q' no longer sets would
  // otherwise survive and keep matching. Same trap subCacheItemCompile has.
  //
  if (sciP->v2P != NULL)
    delete sciP->v2P;

  sciP->v2P = new SubV2Info();

  if (sciP->v2P == NULL)
    KT_X(1, "Out of memory allocating the NGSIv2 state of a Subscription");

  SubV2Info* v2P = sciP->v2P;

  //
  // Everything NGSIv2-only lives under "v2" - see dbModelToApiSubscription and
  // apiModelToCacheSubscription, which both put it there.
  //
  KjNode* v2TreeP = kjLookup(sciP->subTree, "v2");

  //
  // NGSI-LD has no equivalent of the NGSIv2 service path (its "Scope" is not
  // implemented yet), and the NGSI-LD create path has always used "/#" - match
  // any service path. A subscription that came in over NGSIv2 carries its own.
  //
  KjNode* servicePathP = (v2TreeP != NULL)? kjLookup(v2TreeP, "servicePath") : NULL;

  v2P->servicePath = (servicePathP != NULL)? servicePathP->value.s : (char*) "/#";

  //
  // 'q' and 'mq' - the NGSIv2 renderings of the filter
  //
  KjNode* qP  = (v2TreeP != NULL)? kjLookup(v2TreeP, "q")  : NULL;
  KjNode* mqP = (v2TreeP != NULL)? kjLookup(v2TreeP, "mq") : NULL;

  v2P->expression.q  = (qP  != NULL)? qP->value.s  : "";
  v2P->expression.mq = (mqP != NULL)? mqP->value.s : "";

  std::string errorString;

  if ((qP != NULL) && (qP->value.s[0] != 0))
  {
    if (v2P->expression.stringFilter.parse(qP->value.s, &errorString) == false)
      KT_E("Sub '%s': invalid NGSIv2 'q' ('%s'): %s", sciP->subId, qP->value.s, errorString.c_str());
  }

  if ((mqP != NULL) && (mqP->value.s[0] != 0))
  {
    if (v2P->expression.mdStringFilter.parse(mqP->value.s, &errorString) == false)
      KT_E("Sub '%s': invalid NGSIv2 'mq' ('%s'): %s", sciP->subId, mqP->value.s, errorString.c_str());
  }

  //
  // The geo expression, as NGSIv2 wants it - strings, not the GEOS geometry that
  // subCacheItemGeoCompile builds for NGSI-LD.
  //
  KjNode* geoqP = kjLookup(sciP->subTree, "geoQ");

  if (geoqP != NULL)
  {
    KjNode* geometryP    = kjLookup(geoqP, "geometry");
    KjNode* georelP      = kjLookup(geoqP, "georel");
    KjNode* geopropertyP = kjLookup(geoqP, "geoproperty");

    v2P->expression.geometry    = (geometryP    != NULL)? geometryP->value.s    : "";
    v2P->expression.georel      = (georelP      != NULL)? georelP->value.s      : "";
    v2P->expression.geoproperty = (geopropertyP != NULL)? geopropertyP->value.s : "";
  }

  //
  // Notified attributes, and the NGSIv2-only members
  //
  KjNode* notificationP = kjLookup(sciP->subTree, "notification");

  if (notificationP != NULL)
    stringArrayFill(&v2P->attributes, kjLookup(notificationP, "attributes"));

  stringArrayFill(&v2P->metadata, (v2TreeP != NULL)? kjLookup(v2TreeP, "metadata") : NULL);

  KjNode* blacklistP = (v2TreeP != NULL)? kjLookup(v2TreeP, "blacklist") : NULL;

  v2P->blacklist = ((blacklistP != NULL) && (blacklistP->type == KjBoolean))? blacklistP->value.b : false;

  //
  // The endpoint, as NGSIv2 wants it.
  //
  // url, accept and receiverInfo are ordinary API members - they are read from
  // "notification::endpoint". The custom-notification members are NGSIv2-only
  // and come from "v2": with 'custom' set, the notification is built from a
  // template (method, payload, qs) instead of the standard body.
  //
  KjNode* endpointP = (notificationP != NULL)? kjLookup(notificationP, "endpoint") : NULL;

  if (endpointP != NULL)
  {
    KjNode* uriP    = kjLookup(endpointP, "uri");
    KjNode* acceptP = kjLookup(endpointP, "accept");

    if (uriP    != NULL)  v2P->httpInfo.url      = uriP->value.s;
    if (acceptP != NULL)  v2P->httpInfo.mimeType = mimeTypeFromString(acceptP->value.s, NULL, true, false, NULL);

    KjNode* receiverInfoP = kjLookup(endpointP, "receiverInfo");

    if (receiverInfoP != NULL)
    {
      for (KjNode* kvP = receiverInfoP->value.firstChildP; kvP != NULL; kvP = kvP->next)
      {
        KjNode* keyP   = kjLookup(kvP, "key");
        KjNode* valueP = kjLookup(kvP, "value");

        if ((keyP != NULL) && (valueP != NULL))
          v2P->httpInfo.headers[keyP->value.s] = valueP->value.s;
      }
    }
  }

  if (v2TreeP != NULL)
  {
    KjNode* customP  = kjLookup(v2TreeP, "custom");
    KjNode* methodP  = kjLookup(v2TreeP, "method");
    KjNode* payloadP = kjLookup(v2TreeP, "payload");
    KjNode* qsP      = kjLookup(v2TreeP, "qs");

    v2P->httpInfo.custom = ((customP != NULL) && (customP->type == KjBoolean))? customP->value.b : false;

    if (methodP  != NULL)  v2P->httpInfo.verb    = verbFromString(methodP->value.s);
    if (payloadP != NULL)  v2P->httpInfo.payload = payloadP->value.s;

    if (qsP != NULL)
    {
      for (KjNode* kvP = qsP->value.firstChildP; kvP != NULL; kvP = kvP->next)
      {
        if (kvP->type == KjString)
          v2P->httpInfo.qs[kvP->name] = kvP->value.s;
      }
    }
  }

  KT_T(KtSubCache, "Sub '%s': NGSIv2 state compiled (servicePath: '%s', q: '%s', mq: '%s', blacklist: %s, custom: %s, verb: '%s', payload: '%s', qs: %d, headers: %d, metadata: %d)",
       sciP->subId,
       v2P->servicePath,
       v2P->expression.q.c_str(),
       v2P->expression.mq.c_str(),
       (v2P->blacklist == true)? "true" : "false",
       (v2P->httpInfo.custom == true)? "true" : "false",
       verbToString(v2P->httpInfo.verb),
       v2P->httpInfo.payload.c_str(),
       (int) v2P->httpInfo.qs.size(),
       (int) v2P->httpInfo.headers.size(),
       (int) v2P->metadata.size());
}
