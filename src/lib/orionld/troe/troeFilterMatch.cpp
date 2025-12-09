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
#include <string.h>                                            // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "orionld/common/orionldState.h"                       // orionldState, KT_TREE
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/config/configInit.h"                         // configTree



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
  KT_T(KtConfig, "Entity Type: '%s'", entityType);

  KjNode* troeP   = (configTree != NULL)? kjLookup(configTree, "troe") : NULL;
  KjNode* filterP = (troeP != NULL)? kjLookup(troeP, "filter") : NULL;

  if (filterP == NULL)
    return true;

  KT_T(KtConfig, "Entity Type: '%s'", entityType);

  KT_TREE(filterP, "Config::troe::filter", KtConfig);

  int types = 0;
  for (KjNode* f = filterP->value.firstChildP; f != NULL; f = f->next)
  {
    KT_TREE(f, "Config::troe::filter::f", KtConfig);
    KjNode* idP   = kjLookup(f, "id");
    KjNode* typeV = kjLookup(f, "type");

    if (typeV == NULL)
    {
      if (idP != NULL)
      {
        if (strcmp(entityId, idP->value.s) == 0)
        {
          KT_T(KtConfig, "Match! No type and the entity id '%s' matches", entityId);
          return true;
        }
      }
      continue;
    }

    for (KjNode* typeP = typeV->value.firstChildP; typeP != NULL; typeP = typeP->next)
    {
      ++types;
      KT_T(KtConfig, "Entity Type in TRoE filter of config file: '%s'", typeP->value.s);
      if (strcmp(entityType, typeP->value.s) == 0)
      {
        if (idP != NULL)
        {
          if (strcmp(entityId, idP->value.s) == 0)
          {
            KT_T(KtConfig, "Match! No type and the entity id '%s' matches", entityId);
            return true;
          }
        }
        else
        {
          KT_T(KtConfig, "Match! Type matches and ID not present - storing entity of type '%s' in TRoE", entityType);
          return true;
        }
      }
    }
  }

  return false;
}
