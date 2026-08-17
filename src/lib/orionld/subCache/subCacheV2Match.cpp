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
#include <string.h>                                              // strcmp, strlen, strncmp
#include <regex.h>                                               // regexec
#include <string>                                                // std::string
#include <vector>                                                // std::vector

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/SubCache.h"                              // SubCache
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/types/SubEntitySelector.h"                     // SubEntitySelector
#include "orionld/types/SubV2Info.h"                             // SubV2Info
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheV2Match.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// servicePathMatch - does the subscription's servicePath cover this one?
//
// Ported as-is from the legacy sub-cache (cache/subCache.cpp), which is the
// definition of NGSIv2 service-path matching. Only the input changed: the
// subscription's servicePath is a string, not a CachedSubscription.
//
static bool servicePathMatch(const char* spath, const char* servicePath)
{
  if (servicePath == NULL)
    servicePath = "";

  // "/#" as the REQUEST's service path matches anything
  if ((servicePath[0] == '/') && (servicePath[1] == '#') && (servicePath[2] == 0))
    return true;

  if ((servicePath[0] == 0) && (spath[0] == 0))
    return true;

  if (servicePath[0] == 0)
    servicePath = "/";

  if (spath[0] == 0)
    return false;

  // No wildcard - exact match
  if (spath[strlen(spath) - 1] != '#')
    return (strcmp(servicePath, spath) == 0);

  //
  // Wildcard. With a subscription servicePath of "/a/b/#", these match:
  //   1. /a/b
  //   2. /a/b/ and anything below it
  // while "/a/b2" must NOT.
  //
  unsigned int len = strlen(spath) - 2;

  if ((spath[len] == '/') && (strlen(servicePath) == len) && (strncmp(spath, servicePath, len) == 0))
    return true;

  len = strlen(spath) - 1;
  if (strncmp(spath, servicePath, len) == 0)
    return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// attributeMatch - did any of the changed attributes trigger this subscription?
//
// An empty watched-attribute list means "any change" (ONANYCHANGE), so it
// matches. Otherwise the lists must intersect.
//
static bool attributeMatch(SubCacheItem* sciP, const std::vector<std::string>& attrV)
{
  KjNode* watchedP = kjLookup(sciP->subTree, "watchedAttributes");

  if ((watchedP == NULL) || (watchedP->value.firstChildP == NULL))
    return true;

  for (KjNode* watchedAttrP = watchedP->value.firstChildP; watchedAttrP != NULL; watchedAttrP = watchedAttrP->next)
  {
    if (watchedAttrP->type != KjString)
      continue;

    for (unsigned int ix = 0; ix < attrV.size(); ix++)
    {
      if (attrV[ix] == watchedAttrP->value.s)
        return true;
    }
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// entitySelectorMatch - does any entity selector cover this entity?
//
// Each selector is an AND of what it states (id or idPattern, and type), the
// array an OR of the selectors. A selector that states nothing matches anything.
//
// The type half reproduces the legacy EntityInfo::match: a type PATTERN is a
// regex, a plain type must be equal - but an empty type on EITHER side matches,
// which is how an NGSIv2 update without a type reaches a typed subscription.
//
static bool entitySelectorMatch(SubCacheItem* sciP, const char* entityId, const char* entityType)
{
  if (sciP->entitySelectors == NULL)
    return true;

  if (entityId == NULL)
    entityId = "";

  for (SubEntitySelector* sesP = sciP->entitySelectors; sesP != NULL; sesP = sesP->next)
  {
    if (sesP->typeRegexP != NULL)
    {
      if (regexec(sesP->typeRegexP, (entityType != NULL)? entityType : "", 0, NULL, 0) != 0)
        continue;
    }
    else if ((sesP->type != NULL) && (entityType != NULL) && (entityType[0] != 0) && (strcmp(sesP->type, entityType) != 0))
      continue;

    if (sesP->id != NULL)
    {
      if (strcmp(sesP->id, entityId) != 0)
        continue;
    }
    else if (sesP->idPattern != NULL)
    {
      if (sesP->idRegexP == NULL)                                   // The idPattern didn't compile - it matches nothing
        continue;

      if (regexec(sesP->idRegexP, entityId, 0, NULL, 0) != 0)
        continue;
    }

    return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// subCacheV2Match - the NGSIv2 matching, over the new subscription cache
//
// The NGSIv2 counterpart of subCacheAlterationMatch. It answers the same
// question the legacy subCacheMatch did, over the same subscriptions - the cache
// holds every subscription, whichever API created it, and an NGSI-LD one is
// matchable from here on purpose (see subCacheItemV2Compile).
//
// The tenant check the legacy matcher had is GONE: this cache is per tenant, so
// the caller has already chosen the right one - exactly as it went for the
// NGSI-LD matcher.
//
int subCacheV2Match
(
  OrionldTenant*                  tenantP,
  const char*                     servicePath,
  const char*                     entityId,
  const char*                     entityType,
  const std::vector<std::string>& attrV,
  std::vector<SubCacheItem*>*     subVecP
)
{
  SubCache* scP     = (tenantP != NULL)? tenantP->subCache : NULL;
  int       matches = 0;

  if (scP == NULL)
    return 0;

  for (SubCacheItem* sciP = scP->subList; sciP != NULL; sciP = sciP->next)
  {
    const char* spath = ((sciP->v2P != NULL) && (sciP->v2P->servicePath != NULL))? sciP->v2P->servicePath : "/#";

    if (servicePathMatch(spath, servicePath) == false)
    {
      KT_T(KtSubCacheMatch, "Sub '%s': no match due to servicePath", sciP->subId);
      continue;
    }

    if (attributeMatch(sciP, attrV) == false)
    {
      KT_T(KtSubCacheMatch, "Sub '%s': no match due to attributes", sciP->subId);
      continue;
    }

    if (entitySelectorMatch(sciP, entityId, entityType) == false)
    {
      KT_T(KtSubCacheMatch, "Sub '%s': no match due to the entity selectors", sciP->subId);
      continue;
    }

    KT_T(KtSubCacheMatch, "Sub '%s': MATCH", sciP->subId);
    subVecP->push_back(sciP);
    ++matches;
  }

  return matches;
}
