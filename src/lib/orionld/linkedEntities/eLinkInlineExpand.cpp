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
#include "ktrace/kTrace.h"                                       // KTrace
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildAdd, ...
#include "kjson/kjClone.h"                                       // kjClone
}

#include "orionld/types/EntityLink.h"                            // EntityLink
#include "orionld/dds/kjTreeLog.h"                               // kjTreeLog2
#include "orionld/kjTree/kjEntityIdLookupInEntityArray.h"        // kjEntityIdLookupInEntityArray
#include "orionld/common/traceLevels.h"                          // KTrace Levels
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/linkedEntities/eLinkInlineExpand.h"            // Own interface



// -----------------------------------------------------------------------------
//
// eLinkInlineExpand -
//
void eLinkInlineExpand(KjNode* entityP, int level)
{
  KT_T(StLinkedInline, "Level = %d", level);

  if (level >= orionldState.uriParams.joinLevel)
    return;

  if (entityP->type == KjArray)
  {
    KT_T(StLinkedInline, "It's an Array of entities - recursive call for each of the entities");
    for (KjNode* eP = entityP->value.firstChildP; eP != NULL; eP = eP->next)
    {
      eLinkInlineExpand(eP, level + 1);
    }
    return;
  }

  KT_T(StLinkedInline, "It's a single Entity");
  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type != KjObject)
      continue;

    KjNode* objectP = kjLookup(attrP, "object");
    if (objectP == NULL)  // Not a Relationship
      continue;

    KT_T(StLinkedInline, "Found a relationship '%s'", attrP->name);
    KT_T(StLinkedInline, "orionldState.eLinkEntityV at %p", orionldState.eLinkEntityV);
    KjNode* eLinkP = kjEntityIdLookupInEntityArray(orionldState.eLinkEntityV, objectP->value.s);
    if (eLinkP == NULL)
    {
      KT_E("Can't find the entity '%s' in array of linked entities", objectP->value.s);
      continue;
    }
    KjNode* clonedLinkP = kjClone(orionldState.kjsonP, eLinkP);

    KT_T(StLinkedInline, "Found the related entity '%s', inlining it inside the attribute '%s'", objectP->value.s, attrP->name);
    kjTreeLog2(clonedLinkP, "Cloned Entity", StLinkedInline);
    // Add eLinkP as the value of a subAttribute named 'entity' of attrP
    clonedLinkP->name = (char*) "entity";
    kjChildAdd(attrP, clonedLinkP);

    //
    // Add also an "objectType" sub-attribute (the entity type of the referenced entity)
    // Unless it's already there
    //
    if (kjLookup(attrP, "objectType") == NULL)
    {
      KjNode* typeP = kjLookup(clonedLinkP, "type");
      if (typeP != NULL)
      {
        char *  typeLongName = orionldContextItemExpand(orionldState.contextP, typeP->value.s, true, NULL);
        KjNode* objectTypeP  = kjString(orionldState.kjsonP, "objectType", typeLongName);
        kjChildAdd(attrP,  objectTypeP);
      }
    }

    // And finally, recursively call eLinkInlineExpand for the new entity included
    eLinkInlineExpand(clonedLinkP, level + 1);
  }
}
