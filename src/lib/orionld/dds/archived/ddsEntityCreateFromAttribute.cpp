/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
#include "kjson/kjson.h"                                         // Kjson
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd, ...
#include "kjson/kjLookup.h"                                      // kjLookup
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
}

#include "logMsg/logMsg.h"                                       // LM*

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/dds/kjTreeLog.h"                               // kjTreeLog2
#include "orionld/serviceRoutines/orionldPostEntities.h"         // orionldPostEntities - if the entity does not exist
#include "orionld/dds/ddsEntityCreateFromAttribute.h"            // Own Interface



// -----------------------------------------------------------------------------
//
// ddsEntityCreateFromAttribute -
//
int ddsEntityCreateFromAttribute(KjNode* attrNodeP, const char* entityId, const char* attrName)
{
  char* entityType = NULL;

  kjTreeLog2(orionldState.requestTree, "Attribute", StDds);

  // Create entity and continue
  // What do I do if there is no entity type?
  //
  KjNode* entityTypeNodeP = (orionldState.requestTree->type == KjObject)? kjLookup(orionldState.requestTree, "entityType") : NULL;
  if (entityTypeNodeP != NULL)
  {
    kjChildRemove(orionldState.requestTree, entityTypeNodeP);
    entityType = entityTypeNodeP->value.s;
  }
  else
    entityType = (char*) "T";  // FIXME: find the antity type in the config file, if not present as node "entityType" in orionldState.requestTree

  orionldState.payloadIdNode   = kjString(orionldState.kjsonP, "id", entityId);
  orionldState.payloadTypeNode = kjString(orionldState.kjsonP, "type", entityType);
  KT_T(StDds, "Entity doesn't exist - calling orionldPostEntities");
  if (orionldState.requestTree->type != KjObject)
  {
    KT_T(StDds, "But first, need to transform the incoming request tree into a JSON object");
    KjNode* attributeP = orionldState.requestTree;
    attributeP->name = (char*) attrName;
    orionldState.requestTree = kjObject(orionldState.kjsonP, NULL);

    kjChildAdd(orionldState.requestTree, attributeP);
    kjTreeLog2(orionldState.requestTree, "Input KjNode tree to orionldPostEntities", StDds);
  }

  return orionldPostEntities();
}
