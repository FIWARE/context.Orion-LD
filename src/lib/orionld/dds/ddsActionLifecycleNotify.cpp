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
#include "ktrace/kTrace.h"                                       // trace messages
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // StDdsAction
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/types/OrionldAlteration.h"                     // OrionldAlteration, AttributeValueChanged
#include "orionld/notifications/orionldAlterationsTreat.h"       // orionldAlterationsTreat
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup
#include "orionld/dbModel/dbModelToApiEntity.h"                  // dbModelToApiEntity
#include "orionld/troe/troePatchAttribute.h"                     // troePatchAttribute
#include "orionld/dds/ddsActionLifecycleNotify.h"                // Own interface

extern bool troe;



void ddsActionLifecycleNotify
(
  const char* entityId,
  const char* entityType,
  const char* attrLongName,
  KjNode*     attributePayload
)
{
  //
  // 1. Re-read the entity from mongo (the write we just did is now visible)
  //    and convert to API form. This becomes finalApiEntityP on the
  //    alteration record - that's what notificationSend serialises into
  //    the notification body.
  //
  char*   detail      = NULL;
  KjNode* dbEntityP   = mongocEntityLookup(entityId, NULL, NULL, NULL, &detail);

  if (dbEntityP == NULL)
  {
    KT_W("Post-write entity lookup failed for '%s' - skipping subscription/TRoE notify: %s",
         entityId, detail ? detail : "(no detail)");
    return;
  }

  KjNode* apiEntityP = dbModelToApiEntity(dbEntityP, false, (char*) entityId);

  //
  // 2. Build the OrionldAlteration. One altered attribute, AttributeValueChanged
  //    (a new dataset instance, a modified envelope, or an instance pulled -
  //    all are value-level changes from a subscription standpoint).
  //
  OrionldAlteration* altP = (OrionldAlteration*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAlteration));
  bzero(altP, sizeof(OrionldAlteration));

  altP->entityId                    = (char*) entityId;
  altP->entityType                  = (char*) entityType;
  altP->finalApiEntityP             = apiEntityP;
  altP->finalApiEntityWithSysAttrsP = apiEntityP;  // notification body doesn't need sysAttrs by default
  altP->alteredAttributes           = 1;
  altP->alteredAttributeV           = (OrionldAttributeAlteration*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAttributeAlteration));

  char* attrNameEq = kaStrdup(&orionldState.kalloc, attrLongName);
  dotForEq(attrNameEq);

  altP->alteredAttributeV[0].alterationType = AttributeValueChanged;
  altP->alteredAttributeV[0].attrName       = (char*) attrLongName;
  altP->alteredAttributeV[0].attrNameEq     = attrNameEq;

  orionldState.alterations = altP;

  KT_T(StDdsAction, "Dispatching subscription notifications for entity '%s' attr '%s'", entityId, attrLongName);
  orionldAlterationsTreat(altP);

  //
  // 3. TRoE - record an "Update" for the attribute, if --troe is on AND the
  //    caller supplied an attribute payload to record. Pull-on-cleanup
  //    doesn't pass a payload (no useful "what was written" tree); skip.
  //
  if (troe && (attributePayload != NULL))
  {
    KjNode* prevRequestTree   = orionldState.requestTree;
    char*   prevWildcard      = orionldState.wildcard[0];
    char*   prevEntityTypeT   = orionldState.entityTypeForTroe;

    orionldState.wildcard[0]         = (char*) entityId;
    orionldState.entityTypeForTroe   = (char*) entityType;

    // troePatchAttribute reads orionldState.requestTree as the attribute
    // payload (name = FQ attr name, children = type/value/sub-attrs).
    attributePayload->name           = (char*) attrLongName;
    orionldState.requestTree         = attributePayload;

    KT_T(StDdsAction, "Recording TRoE Update for entity '%s' attr '%s'", entityId, attrLongName);
    (void) troePatchAttribute();

    orionldState.requestTree         = prevRequestTree;
    orionldState.wildcard[0]         = prevWildcard;
    orionldState.entityTypeForTroe   = prevEntityTypeT;
  }
}
