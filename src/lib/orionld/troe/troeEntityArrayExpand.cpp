/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjChildRemove
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/context/orionldCoreContext.h"                // orionldCoreContextP
#include "orionld/contextCache/orionldContextCacheLookup.h"    // orionldContextCacheLookup
#include "orionld/context/orionldContextItemExpand.h"          // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"            // orionldAttributeExpand
#include "orionld/context/orionldSubAttributeExpand.h"         // orionldSubAttributeExpand
#include "orionld/context/orionldContextFromTree.h"            // orionldContextFromTree
#include "orionld/troe/troeEntityArrayExpand.h"                // Own interface



// -----------------------------------------------------------------------------
//
// troeEntityArrayExpand -
//
void troeEntityArrayExpand(KjNode* tree)
{
  for (KjNode* entityP = tree->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    //
    // 1. GET @context, extract it if present
    //
    OrionldContext* contextP    = NULL;

    if (orionldState.linkHttpHeaderPresent == true)
      contextP = orionldState.contextP;
    else
    {
      KjNode* contextNodeP = kjLookup(entityP, "@context");

      if (contextNodeP != NULL)
      {
        OrionldProblemDetails  pd;

        kjChildRemove(entityP, contextNodeP);
        contextP = orionldContextFromTree(NULL, OrionldContextFromInline, NULL, contextNodeP, &pd);
      }
    }

    if (contextP == NULL)
      contextP = orionldCoreContextP;

    //
    // FIXME: Call instead troeEntityExpand (currently in troePostEntities.cpp)
    //        Move the function to its own module troe/troeEntityExpand.h/cpp
    //
    for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      if (strcmp(attrP->name, "type") == 0)
        attrP->value.s = orionldContextItemExpand(contextP, attrP->value.s, true, NULL);  // entity type
      else if (strcmp(attrP->name, "id")       == 0) {}
      else
      {
        attrP->name = orionldAttributeExpand(contextP, attrP->name, true, NULL);  // Could be avoided with pCheckEntity

        if (attrP->type == KjObject)
        {
          for (KjNode* subAttrP = attrP->value.firstChildP; subAttrP != NULL; subAttrP = subAttrP->next)
          {
            if      (strcmp(subAttrP->name, "type")        == 0) {}
            else if (strcmp(subAttrP->name, "id")          == 0) {}
            else if (strcmp(subAttrP->name, "value")       == 0) {}
            else if (strcmp(subAttrP->name, "object")      == 0) {}
            else if (strcmp(subAttrP->name, "observedAt")  == 0) {}
            else if (strcmp(subAttrP->name, "location")    == 0) {}
            else if (strcmp(subAttrP->name, "unitCode")    == 0) {}
            else if (strcmp(subAttrP->name, "datasetId")   == 0) {}
            else
              subAttrP->name = orionldSubAttributeExpand(contextP, subAttrP->name, true, NULL);
          }
        }
      }
    }
  }
}
