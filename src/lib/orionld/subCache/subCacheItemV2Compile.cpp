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
  // NGSI-LD has no equivalent of the NGSIv2 service path (its "Scope" is not
  // implemented yet), and the NGSI-LD create path has always used "/#" - match
  // any service path. A subscription that came in over NGSIv2 carries its own.
  //
  KjNode* servicePathP = kjLookup(sciP->subTree, "servicePath");

  v2P->servicePath = (servicePathP != NULL)? servicePathP->value.s : (char*) "/#";

  //
  // 'q' and 'mq' - the NGSIv2 renderings. They are kept in the subTree by
  // apiModelToCacheSubscription for exactly this.
  //
  KjNode* qP  = kjLookup(sciP->subTree, "q");
  KjNode* mqP = kjLookup(sciP->subTree, "mq");

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

  stringArrayFill(&v2P->metadata, kjLookup(sciP->subTree, "metadata"));

  KjNode* blacklistP = kjLookup(sciP->subTree, "blacklist");

  v2P->blacklist = (blacklistP != NULL)? blacklistP->value.b : false;

  KT_T(KtSubCache, "Sub '%s': NGSIv2 state compiled (servicePath: '%s', q: '%s', mq: '%s', blacklist: %s)",
       sciP->subId,
       v2P->servicePath,
       v2P->expression.q.c_str(),
       v2P->expression.mq.c_str(),
       (v2P->blacklist == true)? "true" : "false");
}
