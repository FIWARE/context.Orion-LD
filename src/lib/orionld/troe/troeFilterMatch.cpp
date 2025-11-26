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
#include <string.h>                                              // strcmp

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "orionld/common/orionldState.h"                         // orionldState, LM_TREE
#include "orionld/config/configInit.h"                           // configTree



// -----------------------------------------------------------------------------
//
// troeFilterMatch
//
// {
//   "filter": [
//     {
//       "type": ["https://uri.etsi.org/ngsi-ld/default-context/T1","https://uri.etsi.org/ngsi-ld/default-context/T2" ]
//     }
//   ]
// }
//
bool troeFilterMatch(const char* entityType, const char* entityId)
{
  LM_T(LmtConfig, ("Entity Type: '%s'", entityType));

  KjNode* troeP   = (configTree != NULL)? kjLookup(configTree, "troe") : NULL;
  KjNode* filterP = (troeP != NULL)? kjLookup(troeP, "filter") : NULL;

  if (filterP == NULL)
    return true;

  LM_T(LmtConfig, ("Entity Type: '%s'", entityType));

  LM_TREE(filterP, "Config::troe::filter", LmtConfig);

  int types = 0;
  for (KjNode* f = filterP->value.firstChildP; f != NULL; f = f->next)
  {
    LM_TREE(f, "Config::troe::filter::f", LmtConfig);
    KjNode* idP   = kjLookup(f, "id");
    KjNode* typeV = kjLookup(f, "type");

    if (typeV == NULL)
    {
      if (idP != NULL)
      {
        if (strcmp(entityId, idP->value.s) == 0)
        {
          LM_T(LmtConfig, ("Match! No type and the entity id '%s' matches", entityId));
          return true;
        }
      }
      continue;
    }

    for (KjNode* typeP = typeV->value.firstChildP; typeP != NULL; typeP = typeP->next)
    {
      ++types;
      LM_T(LmtConfig, ("Entity Type in TRoE filter of config file: '%s'", typeP->value.s));
      if (strcmp(entityType, typeP->value.s) == 0)
      {
        if (idP != NULL)
        {
          if (strcmp(entityId, idP->value.s) == 0)
          {
            LM_T(LmtConfig, ("Match! No type and the entity id '%s' matches", entityId));
            return true;
          }
        }
        else
        {
          LM_T(LmtConfig, ("Match! Type matches and ID not present - storing entity of type '%s' in TRoE", entityType));
          return true;
        }
      }
    }
  }

  return false;
}
