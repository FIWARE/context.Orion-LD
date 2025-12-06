/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/common/traceLevels.h"                          // KTrace levels



// -----------------------------------------------------------------------------
//
// orionldContextTreePresent -
//
void orionldContextTreePresent(const char* prefix, KjNode* contextNodeP)
{
  if (contextNodeP->type == KjString)
    KT_T(KtContextTree, "%s: the context is a String: %s", prefix, contextNodeP->value.s);
  else if (contextNodeP->type == KjArray)
  {
    KT_T(KtContextTree, "%s: the context is an Array:", prefix);
    for (KjNode* aItemP = contextNodeP->value.firstChildP; aItemP != NULL; aItemP = aItemP->next)
      orionldContextTreePresent(prefix, aItemP);
  }
  else if (contextNodeP->type == KjObject)
  {
    int items = 0;

    KT_T(KtContextTree, "%s: the context is an Object:", prefix);
    for (KjNode* itemP = contextNodeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
    {
      if (itemP->type == KjString)
        KT_T(KtContextTree, "%s: %s -> %s", prefix, itemP->name, itemP->value);
      else
        KT_T(KtContextTree, "%s: %s (%s)", prefix, itemP->name, kjValueType(itemP->type));
      ++items;
      if (items > 3)
        break;
    }
  }
  else
    KT_T(KtContextTree, "%s: Invalid type for context: %s", prefix, kjValueType(contextNodeP->type));

  KT_T(KtContextTree, "%s", prefix);
}

