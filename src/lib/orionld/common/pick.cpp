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
#include "kjson/KjNode.h"                                           // KjNode
#include "kjson/kjBuilder.h"                                        // kjChildRemove
#include "kjson/kjLookup.h"                                         // kjLookup
}

#include "logMsg/logMsg.h"                                          // LM_*

#include "orionld/types/StringArray.h"                              // StringArray, stringArrayLookup
#include "orionld/common/orionldState.h"                            // orionldState
#include "orionld/common/pick.h"                                    // Own interface



// -----------------------------------------------------------------------------
//
// pickForEntity -
//
void pickForEntity(KjNode* entityP)
{
  KjNode* idP = kjLookup(entityP, "id");
  LM_T(LmtPick, ("  o %s", idP->value.s));

  KjNode* next     = NULL;
  KjNode* itemP    = entityP->value.firstChildP;
#ifdef LM_ON
  char*   itemName = NULL;
#endif

  while (itemP != NULL)
  {
#ifdef LM_ON
    itemName = itemP->name;
#endif

    next     = itemP->next;

    if (stringArrayLookup(&orionldState.in.pickList, itemP->name) == false)
    {
      kjChildRemove(entityP, itemP);
      itemP = NULL;
    }

    LM_T(LmtPick, ("    - %s (%s)", itemName, (itemP == NULL)? "removed" : "stays"));
    itemP = next;
  }
}



// -----------------------------------------------------------------------------
//
// pickForEntityArray -
//
void pickForEntityArray(void)
{
  LM_T(LmtPick, ("%d items in pick: %d", orionldState.in.pickList.items));

  for (int ix = 0; ix < orionldState.in.pickList.items; ix++)
  {
    LM_T(LmtPick, ("  o %s", orionldState.in.pickList.array[ix]));
  }

  LM_T(LmtPick, ("First level records per entity:"));
  for (KjNode* entityP = orionldState.responseTree->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    pickForEntity(entityP);
  }
}
