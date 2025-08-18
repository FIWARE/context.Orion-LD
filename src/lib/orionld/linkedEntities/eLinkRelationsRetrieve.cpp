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
#include "orionld/serviceRoutines/orionldGetEntity.h"            // orionldGetEntity
#include "orionld/linkedEntities/eLinkDebug.h"                   // eLinkDebug
#include "orionld/linkedEntities/eLinkInlineExpand.h"            // Own interface



// -----------------------------------------------------------------------------
//
// eLinkEntityLookup -
//
KjNode* eLinkEntityLookup(KjNode* entityV, const char* entityId)
{
  for (KjNode* entityP = entityV->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    KjNode* idP = kjLookup(entityP, "id");

    if (idP != NULL)
    {
     if (strcmp(idP->value.s, entityId) == 0)
        return entityP;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// eLinkEntityRetrieve -
//
static KjNode* eLinkEntityRetrieve(KjNode* entityV, const char* entityId, KjNode* attrP)
{
  KT_T(StLinked, "--------- Retreiving linked entity '%s'", entityId);

  // Preparing orionldState and calling orionldGetEntity again
  orionldState.responseTree = NULL;
  orionldState.wildcard[0]  = (char*) entityId;

  // We have to change the URL PATH as well, as it is used during distops
  char urlPath[256];
  snprintf(urlPath, sizeof(urlPath) - 1, "/ngsi-ld/v1/entities/%s", entityId);
  orionldState.urlPath = urlPath;

  //
  // Calling the service routine to retrieve the entity in question
  // UNLESS we have already retrieved the entity
  //
  KjNode* eP = eLinkEntityLookup(entityV, entityId);

  if (eP != NULL)
    return eP;

  if (orionldGetEntity() == true)
  {
    if ((orionldState.httpStatusCode == 200) && (orionldState.responseTree != NULL))
    {
      KT_T(StLinked, "Adding entity '%s' to the entity array (need the attribute as well, for inline format)", entityId);
      kjChildAdd(entityV, orionldState.responseTree);
      return orionldState.responseTree;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// eLinkPairAdd -
//
static void eLinkPairAdd(KjNode* entityP, KjNode* attrP)
{
  EntityLink* eLinkP = (EntityLink*) kaAlloc(&orionldState.kalloc, sizeof(EntityLink));

  eLinkP->entityP = entityP;
  eLinkP->attrP   = attrP;
  eLinkP->next    = orionldState.eLinkList;

  orionldState.eLinkList = eLinkP;

  // <DEBUG>
  KjNode*       idP    = kjLookup(eLinkP->entityP, "id");
  const char*   eId    = (idP != NULL)? idP->value.s : "noname";
  const char*   aName  = eLinkP->attrP->name;

  KT_T(StLinkedInline, "Added an EntityLink, entityP (%s) at %p and attrP (%s) at %p", eId, eLinkP->entityP, aName, eLinkP->attrP);
  // </DEBUG>
}



// -----------------------------------------------------------------------------
//
// eLinkRelationsRetrieve -
//
void eLinkRelationsRetrieve(KjNode* entityV, KjNode* entityP, int level)
{
  // <DEBUG>
  KjNode* idP = kjLookup(entityP, "id");
  KT_T(StLinked, "================================= Level %d ========================", level);
  KT_T(StLinked, "Getting relationship entities for entity '%s' (join level %d)", idP->value.s, orionldState.uriParams.joinLevel);


  // -----------------------------------------------------------------------------
  //
  // <DEBUG>
  //
  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type != KjObject)
      continue;

    KjNode* objectP = kjLookup(attrP, "object");
    if (objectP == NULL)
      continue;

    KT_T(StLinked, "Relationship Entity: '%s'", objectP->value.s);
  }
  // </DEBUG>


  // -----------------------------------------------------------------------------
  //
  // 1. GET all referenced relationship entities and add them to the entity array
  //
  KjNode* lastInLevel = entityP;  // eLinkRelationsRetrieve keeps adding higher level entities at the end of the array

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    KjNode* current;
    if (attrP->type != KjObject)
      continue;

    KjNode* objectP = kjLookup(attrP, "object");

    if (objectP == NULL)
      continue;

    if (objectP->type == KjString)
    {
      KT_T(StLinked, "The entity '%s' is referenced by the attribute '%s' at %p (need to save this attr pointer for inline mode)", objectP->value.s, attrP->name);
      current = eLinkEntityRetrieve(entityV, objectP->value.s, attrP);
      if (current != NULL)
      {
        lastInLevel = current;
        eLinkPairAdd(current, attrP);
      }
    }
    else if (objectP->type == KjArray)
    {
      for (KjNode* eIdP = objectP->value.firstChildP; eIdP != NULL; eIdP = eIdP->next)
      {
        KT_T(StLinked, "The entity '%s' is referenced by the attribute '%s' at %p (need to save this attr pointer for inline mode)", eIdP->value.s, attrP->name);
        current = eLinkEntityRetrieve(entityV, eIdP->value.s, attrP);
        if (current != NULL)
        {
          lastInLevel = current;
          eLinkPairAdd(current, attrP);
        }
      }
    }
  }
  eLinkDebug(entityV, "After adding referenced entities");


  // -----------------------------------------------------------------------------
  //
  // 2. Recursive call for each of the new entities (referenced by entityP)
  //
  if (level + 1 == orionldState.uriParams.joinLevel)
    return;

  for (KjNode* eP = entityP->next; eP != NULL; eP = eP->next)
  {
    eLinkRelationsRetrieve(entityV, eP, level + 1);
    if (eP == lastInLevel)
      break;
  }
}
