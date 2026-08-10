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
#include <stdlib.h>                                              // calloc
#include <regex.h>                                               // regcomp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/types/SubEntitySelector.h"                     // SubEntitySelector
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheItemEntitiesCompile.h"        // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemEntitiesCompile -
//
void subCacheItemEntitiesCompile(SubCacheItem* sciP, KjNode* entitiesP)
{
  SubEntitySelector* last = NULL;

  for (KjNode* eSelectorP = entitiesP->value.firstChildP; eSelectorP != NULL; eSelectorP = eSelectorP->next)
  {
    KjNode* idP            = kjLookup(eSelectorP, "id");
    KjNode* idPatternP     = kjLookup(eSelectorP, "idPattern");
    KjNode* typeP          = kjLookup(eSelectorP, "type");
    KjNode* isTypePatternP = kjLookup(eSelectorP, "isTypePattern");

    SubEntitySelector* sesP = (SubEntitySelector*) calloc(1, sizeof(SubEntitySelector));

    if (sesP == NULL)
      KT_X(1, "Out of memory attempting to allocate a Subscription Entity Selector (%d bytes)", sizeof(SubEntitySelector));

    //
    // An EMPTY string is not a selector, it is the absence of one - the database
    // model of an NGSIv2 subscription (and of an NGSI-LD one created over the
    // legacy driver) spells "any entity id" as "id": "" and "any type" as
    // "type": "". Compiled as a literal, "" would match nothing at all.
    //
    sesP->owner = eSelectorP;
    sesP->id    = ((idP   != NULL) && (idP->value.s[0]   != 0))? idP->value.s   : NULL;
    sesP->type  = ((typeP != NULL) && (typeP->value.s[0] != 0))? typeP->value.s : NULL;

    //
    // An "id" and an "idPattern" are mutually exclusive - "id" wins, exactly as
    // the old cache had it. Neither of them means "any entity id".
    //
    if ((sesP->id == NULL) && (idPatternP != NULL) && (idPatternP->value.s[0] != 0))
    {
      sesP->idPattern = idPatternP->value.s;

      if (regcomp(&sesP->idRegex, sesP->idPattern, REG_EXTENDED) == 0)
        sesP->idRegexP = &sesP->idRegex;
      else
        KT_E("Sub '%s': error compiling the regex for idPattern '%s' - the pattern will match nothing", sciP->subId, sesP->idPattern);
    }

    //
    // NGSIv2's "typePattern" - the type is a regex, not a name. dbModelToApiSubscription
    // only leaves 'isTypePattern' in the tree when it is set, and never for NGSI-LD.
    //
    if ((sesP->type != NULL) && (isTypePatternP != NULL) && (isTypePatternP->type == KjBoolean) && (isTypePatternP->value.b == true))
    {
      if (regcomp(&sesP->typeRegex, sesP->type, REG_EXTENDED) == 0)
        sesP->typeRegexP = &sesP->typeRegex;
      else
        KT_E("Sub '%s': error compiling the regex for typePattern '%s' - the pattern will match nothing", sciP->subId, sesP->type);
    }

    KT_T(KtSubCache, "Sub '%s': entity selector (id: '%s', idPattern: '%s', type: '%s'%s)",
         sciP->subId, sesP->id, sesP->idPattern, sesP->type, (sesP->typeRegexP != NULL)? " (a pattern)" : "");

    // Append - keep the order of the "entities" array
    if (last == NULL)
      sciP->entitySelectors = sesP;
    else
      last->next = sesP;
    last = sesP;
  }
}
