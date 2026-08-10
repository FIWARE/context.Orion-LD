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
#include <string.h>                                              // strchr
#include <string>                                                // std::string
#include <vector>                                                // std::vector

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjString
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/types/SubV2Info.h"                             // SubV2Info
#include "orionld/types/Verb.h"                                  // verbFromString, verbToString

#include "rest/StringFilter.h"                                    // StringFilter
#include "common/MimeType.h"                                     // mimeTypeFromString
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/q/qAliasCompact.h"                             // qAliasCompact
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
// stringFilterBuild - parse a 'q'/'mq' from the database into a StringFilter
//
// StringFilter::parse wants the 'q' exactly as an NGSIv2 API request spells it -
// SHORT attribute names. It expands them itself (with the current @context) and
// encodes the dots of the expanded IRI as '=', which is how mongo stores them.
//
// What the database holds is the far end of that road: expanded AND '='-encoded.
// Handing it straight back to the parser does not work, in either direction:
//
//   "https://uri=etsi=org/...temperature>20"  - '=' is an NGSIv2 operator, so the
//                                               name ends at the first one
//   "https://uri.etsi.org/...temperature>20"  - '.' is the compound-path separator,
//                                               so the name ends at the first dot
//
// Both leave a filter looking for an attribute called "https://uri", which no
// entity has - so the filter rejects EVERY entity and the subscription silently
// stops notifying. So the road is walked backwards first: qAliasCompact turns the
// '=' back into dots and the expanded IRIs back into their short names.
//
// It edits the string it is given and answers in memory of its own, so it is fed
// a copy - the subTree keeps the subscription as the database has it.
//
static void stringFilterBuild(SubCacheItem* sciP, StringFilter* stringFilterP, const char* member, char* dbQ)
{
  std::string  errorString;

  //
  // A 'q' with an OR in it has no NGSIv2 equivalent - NGSIv2 only knows AND. The
  // write path already deals with that: it stores the never-matching "P;!P"
  // ("P Exists AND P Does Not Exist") so that the NGSIv2 side stays out of the way
  // and the subscription is decided by the NGSI-LD 'q' (the QNode tree) alone.
  //
  // Both spellings are taken as-is: "P;!P" holds no attribute name to translate,
  // and running it through the compaction below would turn it into a filter that
  // matches EVERYTHING - the exact opposite of what it is there for.
  //
  if ((strcmp(dbQ, "P;!P") == 0) || (strchr(dbQ, '|') != NULL))
  {
    if (stringFilterP->parse((char*) "P;!P", &errorString) == false)
      KT_E("Sub '%s': unable to build the never-matching NGSIv2 '%s': %s", sciP->subId, member, errorString.c_str());

    KT_T(KtSubCache, "Sub '%s': NGSIv2 '%s' is the never-matching filter ('%s') - the NGSI-LD 'q' decides", sciP->subId, member, dbQ);
    return;
  }

  //
  // Only an NGSI-LD subscription has been down that road - NGSIv2 does not expand
  // anything, so the 'q' of an NGSIv2 subscription is already exactly what the
  // parser wants and is handed over untouched, as the old sub-cache did.
  //
  char* q = dbQ;

  if (sciP->ngsild == true)
  {
    KjNode* qNodeP = kjString(orionldState.kjsonP, member, kaStrdup(&orionldState.kalloc, dbQ));

    if (qAliasCompact(qNodeP, true) == false)
      KT_E("Sub '%s': unable to compact the NGSIv2 '%s' ('%s')", sciP->subId, member, dbQ);

    q = qNodeP->value.s;
    KT_T(KtSubCache, "Sub '%s': NGSIv2 '%s' '%s' compacted to '%s'", sciP->subId, member, dbQ, q);
  }

  if (stringFilterP->parse(q, &errorString) == false)
    KT_E("Sub '%s': invalid NGSIv2 '%s' ('%s'): %s", sciP->subId, member, q, errorString.c_str());
}



// -----------------------------------------------------------------------------
//
// subCacheItemV2Compile - build the NGSIv2 matching state of a cached Subscription
//
// EVERY subscription gets one, not only the NGSIv2-created ones. An entity
// updated through the NGSIv2 API is matched against the subscription cache by
// subCacheV2Match, and that has always included the NGSI-LD subscriptions: the
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

  if ((qP != NULL) && (qP->value.s[0] != 0))
    stringFilterBuild(sciP, &v2P->expression.stringFilter, "q", qP->value.s);

  if ((mqP != NULL) && (mqP->value.s[0] != 0))
    stringFilterBuild(sciP, &v2P->expression.mdStringFilter, "mq", mqP->value.s);

  //
  // The geo expression, as NGSIv2 wants it - strings, not the GEOS geometry that
  // subCacheItemGeoCompile builds for NGSI-LD.
  //
  //
  // 'geometry', 'georel' and 'coords' come from "v2" - the database's own spelling,
  // which is what the NGSIv2 geo filter reads. "geoQ" holds the NGSI-LD rewriting
  // of the very same thing ("Point" for "point", an Array of coordinates, ...) and
  // the NGSIv2 side cannot read a word of it. See dbModelToApiSubscription.
  //
  // 'geoproperty' is spelled the same either way, so "geoQ" will do for it.
  //
  KjNode* geometryP = (v2TreeP != NULL)? kjLookup(v2TreeP, "geometry") : NULL;
  KjNode* georelP   = (v2TreeP != NULL)? kjLookup(v2TreeP, "georel")   : NULL;
  KjNode* coordsP   = (v2TreeP != NULL)? kjLookup(v2TreeP, "coords")   : NULL;

  v2P->expression.geometry = (geometryP != NULL)? geometryP->value.s : "";
  v2P->expression.georel   = (georelP   != NULL)? georelP->value.s   : "";
  v2P->expression.coords   = (coordsP   != NULL)? coordsP->value.s   : "";

  KjNode* geoqP = kjLookup(sciP->subTree, "geoQ");

  if (geoqP != NULL)
  {
    KjNode* geopropertyP = kjLookup(geoqP, "geoproperty");

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
