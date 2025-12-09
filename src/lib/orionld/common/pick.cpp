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
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                           // KjNode
#include "kjson/kjBuilder.h"                                        // kjChildRemove
#include "kjson/kjLookup.h"                                         // kjLookup
}

#include "orionld/types/StringArray.h"                              // StringArray, stringArrayLookup
#include "orionld/common/orionldState.h"                            // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/pick.h"                                    // Own interface



// -----------------------------------------------------------------------------
//
// pickForEntity -
//
void pickForEntity(KjNode* entityP)
{
  KjNode* idP      = kjLookup(entityP, "id");
  KjNode* next     = NULL;
  KjNode* itemP    = entityP->value.firstChildP;
  char*   itemName = NULL;

  if (idP != NULL)
    KT_T(KtPick, "  o %s", idP->value.s);

  while (itemP != NULL)
  {
    itemName = itemP->name;
    next     = itemP->next;

    if (stringArrayLookup(&orionldState.in.pickList, itemP->name) == false)
    {
      kjChildRemove(entityP, itemP);
      itemP = NULL;
    }

    KT_T(KtPick, "    - %s (%s)", itemName, (itemP == NULL)? "removed" : "stays");
    itemP = next;
  }
}



// -----------------------------------------------------------------------------
//
// pickForEntityArray -
//
void pickForEntityArray(void)
{
  KT_T(KtPick, "%d items in pick: %d", orionldState.in.pickList.items);

  for (int ix = 0; ix < orionldState.in.pickList.items; ix++)
  {
    KT_T(KtPick, "  o %s", orionldState.in.pickList.array[ix]);
  }

  KT_T(KtPick, "First level records per entity:");
  for (KjNode* entityP = orionldState.responseTree->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    pickForEntity(entityP);
  }
}
