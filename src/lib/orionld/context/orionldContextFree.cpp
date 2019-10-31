/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

extern "C"
{
#include "kjson/kjFree.h"                                      // kjFree
}

#include "orionld/context/OrionldContext.h"                    // OrionldContext
#include "orionld/context/orionldCoreContext.h"                // orionldCoreContext
#include "orionld/context/orionldContextFree.h"                // Own interface



// -----------------------------------------------------------------------------
//
// orionldContextFree -
//
void orionldContextFree(OrionldContext* contextP)
{
  if (contextP == NULL)
  {
    LM_T(LmtContext, ("NOT Freeing LIST context (NULL)"));
    return;
  }

  LM_T(LmtContext, ("Freeing context %p, tree:%p, next:%p, url:%s", contextP, contextP->tree, contextP->next, contextP->url));

  if (contextP->tree != NULL)
  {
    kjFree(contextP->tree);
    contextP->tree = NULL;
  }

  if (contextP->type == OrionldUserContext)
  {
    if (contextP->url != NULL)
      free(contextP->url);
    free(contextP);
  }
}
