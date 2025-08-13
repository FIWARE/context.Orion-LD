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
}

#include "orionld/types/EntityLink.h"                            // EntityLink
#include "orionld/common/traceLevels.h"                          // KTrace Levels
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/linkedEntities/eLinkInlineExpand.h"            // Own interface



// -----------------------------------------------------------------------------
//
// eLinkInlineExpand -
//
void eLinkInlineExpand(void)
{
  // <DEBUG>
  for (EntityLink* eLinkP = orionldState.eLinkList; eLinkP != NULL; eLinkP = eLinkP->next)
  {
    KT_T(StLinkedInline, "--------------------------------");
    KT_T(StLinkedInline, "eLinkP at %p", eLinkP);
    KT_T(StLinkedInline, "eLinkP->entityP at %p", eLinkP->entityP);
    KjNode*       idP    = kjLookup(eLinkP->entityP, "id");
    KT_T(StLinkedInline, "idP at %p", idP);
    const char*   eId    = (idP != NULL)? idP->value.s : "noname";
    KT_T(StLinkedInline, "eId: '%s'", eId);
    const char*   aName  = eLinkP->attrP->name;
    KT_T(StLinkedInline, "attr: '%s'", aName);

    KT_T(StLinkedInline, "* Got a Linked Entity '%s' of Relationship '%s'", eId, aName);
    KT_T(StLinkedInline, "--------------------------------");
  }
  // </DEBUG>

  EntityLink* next;
  EntityLink* eLinkP = orionldState.eLinkList;

  while (eLinkP != NULL)
  {
    next = eLinkP->next;

    KjNode*       idP    = kjLookup(eLinkP->entityP, "id");
    const char*   eId    = (idP != NULL)? idP->value.s : "noname";
    const char*   aName  = eLinkP->attrP->name;

    KT_T(StLinkedInline, "Linked Entity '%s' to be moved to Relationship '%s'", eId, aName);

    // Add eLinkP as the value of a subAttribute named 'entity' of attrP
    eLinkP->entityP->name = (char*) "entity";
    kjChildAdd(eLinkP->attrP, eLinkP->entityP);

    // Add also an "objectType" sub-attribute (the entity type of the references entity)
    KjNode* typeP = kjLookup(eLinkP->entityP, "type");
    if (typeP != NULL)
    {
      char *  typeLongName = orionldContextItemExpand(orionldState.contextP, typeP->value.s, true, NULL);
      KjNode* objectTypeP  = kjString(orionldState.kjsonP, "objectType", typeLongName);

      kjChildAdd(eLinkP->attrP,  objectTypeP);
    }

    eLinkP = next;
  }
}
