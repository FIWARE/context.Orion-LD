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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjParse.h"                                       // kjParse
}

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "orionld/common/orionldState.h"                         // kjsonP
#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails, orionldProblemDetailsFill
#include "orionld/common/orionldErrorResponse.h"                 // OrionldBadRequestData
#include "orionld/context/OrionldContext.h"                      // OrionldContext
#include "orionld/context/orionldContextFromTree.h"              // orionldContextFromTree
#include "orionld/context/orionldContextFromBuffer.h"            // Own interface



// -----------------------------------------------------------------------------
//
// orionldContextFromBuffer -
//
OrionldContext* orionldContextFromBuffer(char* url, char* buffer, OrionldProblemDetails* pdP)
{
  KjNode* tree = kjParse(kjsonP, buffer);

  if (tree == NULL)
  {
    LM_E(("JSON Parse Error in @context '%s'", url));
    pdP->type   = OrionldBadRequestData;
    pdP->title  = (char*) "JSON Parse Error in @context";
    pdP->detail = url;
    pdP->status = 400;

    return NULL;
  }

  KjNode* contextNodeP = kjLookup(tree, "@context");
  if (contextNodeP == NULL)
  {
    LM_E(("No @context field in the context"));
    pdP->type   = OrionldBadRequestData;
    pdP->title  = (char*) "Invalid context - @context field missing";
    pdP->detail = url;
    pdP->status = 400;

    return NULL;
  }

  return orionldContextFromTree(url, false, contextNodeP, pdP);
}
