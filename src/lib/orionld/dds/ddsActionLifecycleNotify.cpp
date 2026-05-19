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
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/types/OrionldAlteration.h"                     // OrionldAlteration, AttributeValueChanged
#include "orionld/notifications/orionldAlterationsTreat.h"       // orionldAlterationsTreat
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup
#include "orionld/dbModel/dbModelToApiEntity.h"                  // dbModelToApiEntity
#include "orionld/dds/ddsActionLifecycleNotify.h"                // Own interface



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
  // Subscription cache stores entity types in their FQ (expanded) form. The
  // entityType we get from the DDS action mapping is the short form (as it
  // appears in the user-side configFile and as it was used for the original
  // PATCH that triggered the goal) - expand it so the matcher's strcmp on
  // the entity type succeeds.
  char* expandedType = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);

  OrionldAlteration* altP = (OrionldAlteration*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAlteration));
  bzero(altP, sizeof(OrionldAlteration));

  altP->entityId                    = (char*) entityId;
  altP->entityType                  = expandedType;
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

  // TODO: TRoE.
  //   A direct call to troePatchAttribute() with our attribute payload
  //   crashes the broker on the second invocation (pgCommands path is
  //   sensitive to the orionldState arena reset between DDS callbacks).
  //   The TRoE pipeline as it stands isn't safe to drive from a non-MHD
  //   thread with our dataset-instance shape. Coming back to this once
  //   the TRoE layer learns about @datasets instances natively.
  (void) entityType;
  (void) attributePayload;
}
