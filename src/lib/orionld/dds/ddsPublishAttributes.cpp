/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjClone.h"                                       // kjClone
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldPatchApply.h"                    // orionldPatchApply
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/dds/ddsPublishAttribute.h"                     // ddsPublishAttribute



// ----------------------------------------------------------------------------
//
// ddsPublishAttributes -
//
// NOTE:
//   This function is used for the service routine "PATCH /entities/{entityId}",
//   where the final attribute is still not known - need to merge it with the DB content.
//
void ddsPublishAttributes(const char* entityId, KjNode* incoming, KjNode* dbAttrsP)
{
  KT_T(StDds, "Pushing attributes to DDS");

  KjNode* patchTree = orionldState.requestTree;
  KjNode* patchBase = kjClone(orionldState.kjsonP, orionldState.patchBase);

  for (KjNode* patchP = patchTree->value.firstChildP; patchP != NULL; patchP = patchP->next)
  {
    orionldPatchApply(patchBase, patchP, false);
  }

  // patchBase is now fully merged
  // kjTreeLog2(patchBase, "patchBase", StDds);

  for (KjNode* attrP = patchBase->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (strcmp(attrP->name, "id")    == 0)  continue;
    if (strcmp(attrP->name, "type")  == 0)  continue;
    if (strcmp(attrP->name, "scope") == 0)  continue;

    KT_T(StDds, "Attribute is '%s'", attrP->name);
    char*        longName  = kaStrdup(&orionldState.kalloc, attrP->name);
    eqForDot(longName);

    char* shortName = orionldContextItemAliasLookup(orionldState.contextP, longName, NULL, NULL);

    ddsPublishAttribute(entityId, shortName, attrP, false);
  }
}
