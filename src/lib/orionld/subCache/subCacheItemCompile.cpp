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
extern "C"
{
#include "kbase/kMacros.h"                                       // K_FT
#include "kbase/kTime.h"                                         // kTimeGet
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldRenderFormat.h"                   // OrionldRenderFormat, renderFormat
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/dateTime.h"                             // dateTimeFromString
#include "orionld/q/qBuild.h"                                    // qBuild
#include "orionld/subCache/subCacheItemEndpointCompile.h"        // subCacheItemEndpointCompile
#include "orionld/subCache/subCacheItemEntitiesCompile.h"        // subCacheItemEntitiesCompile
#include "orionld/subCache/subCacheItemGeoCompile.h"             // subCacheItemGeoCompile
#include "orionld/subCache/subCacheItemCompile.h"                // Own interface



// -----------------------------------------------------------------------------
//
// numberFrom - a timestamp in the subscription tree is either a Float or a String
//
static double numberFrom(KjNode* nodeP)
{
  if (nodeP->type == KjFloat)
    return nodeP->value.f;

  if (nodeP->type == KjInt)
    return nodeP->value.i;

  char errorString[256];

  return dateTimeFromString(nodeP->value.s, errorString, sizeof(errorString));
}



// -----------------------------------------------------------------------------
//
// expirationCompile - 'expiresAt', and what an already passed 'expiresAt' means
//
static void expirationCompile(SubCacheItem* sciP, KjNode* expiresAtP)
{
  struct timespec now;

  sciP->expiresAt = numberFrom(expiresAtP);

  if (sciP->expiresAt <= 0)  // No expiration date - the subscription lives forever
    return;

  kTimeGet(&now);

  if (sciP->expiresAt > now.tv_sec + now.tv_nsec / 1000000000.0)
    return;

  //
  // The subscription outlived its expiration - it stays in the cache (a PATCH can
  // give it a new 'expiresAt') but it neither matches nor notifies.
  //
  sciP->isActive = false;

  KjNode* statusP = kjLookup(sciP->subTree, "status");

  if (statusP != NULL)
    statusP->value.s = (char*) "expired";  // kjFree doesn't free a KjString's value - a literal is safe here

  KT_T(KtSubCache, "Sub '%s': expired (expiresAt: %f)", sciP->subId, sciP->expiresAt);
}



// -----------------------------------------------------------------------------
//
// notificationCompile - render format, triggers, and the endpoint
//
static void notificationCompile(SubCacheItem* sciP, KjNode* notificationP)
{
  KjNode* formatP      = kjLookup(notificationP, "format");
  KjNode* showChangesP = kjLookup(notificationP, "showChanges");
  KjNode* sysAttrsP    = kjLookup(notificationP, "sysAttrs");
  KjNode* endpointP    = kjLookup(notificationP, "endpoint");

  sciP->renderFormat = (formatP != NULL)? renderFormat(formatP->value.s) : RF_NONE;

  if (sciP->renderFormat == RF_NONE)
    sciP->renderFormat = RF_DEFAULT;

  sciP->showChanges = (showChangesP != NULL)? showChangesP->value.b : false;
  sciP->sysAttrs    = (sysAttrsP    != NULL)? sysAttrsP->value.b    : false;

  if (endpointP != NULL)
    subCacheItemEndpointCompile(sciP, endpointP);
}



// -----------------------------------------------------------------------------
//
// subCacheItemCompile -
//
void subCacheItemCompile(SubCacheItem* sciP)
{
  KjNode* isActiveP     = kjLookup(sciP->subTree, "isActive");
  KjNode* expiresAtP    = kjLookup(sciP->subTree, "expiresAt");
  KjNode* throttlingP   = kjLookup(sciP->subTree, "throttling");
  KjNode* langP         = kjLookup(sciP->subTree, "lang");
  KjNode* entitiesP     = kjLookup(sciP->subTree, "entities");
  KjNode* geoqP         = kjLookup(sciP->subTree, "geoQ");
  KjNode* notificationP = kjLookup(sciP->subTree, "notification");

  //
  // 'q' is stored expanded, under the name "ldQ" - that is the one that matches
  // entities. A plain "q" is the NGSIv2 rendering of the very same filter and is
  // NOT usable for NGSI-LD matching, so it is deliberately not a fallback here.
  // The API-request path must hand over its 'q' under the name "ldQ" as well.
  //
  KjNode* qP = kjLookup(sciP->subTree, "ldQ");

  sciP->isActive = (isActiveP != NULL)? isActiveP->value.b : true;

  //
  // The API has no way to ask for a subset of the alteration types -
  // "notificationTrigger" is answered with a 501 - so every trigger is on.
  //
  sciP->triggers = SUB_TRIGGERS_ALL;

  if (expiresAtP != NULL)
    expirationCompile(sciP, expiresAtP);

  if (throttlingP != NULL)
    sciP->throttling = (throttlingP->type == KjFloat)? throttlingP->value.f : throttlingP->value.i;

  if (langP != NULL)
    sciP->lang = langP->value.s;

  if ((qP != NULL) && (qP->value.s[0] != 0))
  {
    sciP->qText = qP->value.s;
    sciP->qP    = qBuild(sciP->qText, NULL, NULL, NULL, false, false);  // qBuild mallocs - the QNode tree outlives the request

    if (sciP->qP == NULL)
      KT_E("Sub '%s': invalid 'q' ('%s') - the subscription will not filter on it", sciP->subId, sciP->qText);
  }

  if (entitiesP != NULL)
    subCacheItemEntitiesCompile(sciP, entitiesP);

  if (geoqP != NULL)
    subCacheItemGeoCompile(sciP, geoqP);

  if (notificationP != NULL)
    notificationCompile(sciP, notificationP);

  KT_T(KtSubCache, "Sub '%s': compiled (isActive: %s, q: %s, geoQ: %s, renderFormat: '%s')",
       sciP->subId,
       K_FT(sciP->isActive),
       K_FT(sciP->qP      != NULL),
       K_FT(sciP->geoInfo != NULL),
       renderFormat(sciP->renderFormat));
}
