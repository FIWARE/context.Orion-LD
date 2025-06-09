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
#include <unistd.h>                                           // NULL
#include <string.h>                                           // strcmp

extern "C"
{
#include "kjson/KjNode.h"                                     // KjNode
#include "kjson/kjLookup.h"                                   // kjLookup
#include "ktrace/kTrace.h"                                    // trace messages - ktrace library
}

#include "orionld/common/traceLevels.h"                       // KT tracelevels
#include "orionld/config/configInit.h"                        // configTree
#include "orionld/kjTree/kjNavigate.h"                        // kjNavigate
#include "orionld/dds/kjTreeLog.h"                            // kjTreeLog2
#include "orionld/config/configAttributeToDdsTopic.h"         // Own interface



// -----------------------------------------------------------------------------
//
// configAttributeToDdsTopic -
//
char* configAttributeToDdsTopic(const char* entityId, const char* attributeShortName)
{
  if (configTree == NULL)
    KT_RE(NULL, "No Config File");

  const char*    path[4] = { "dds", "ngsild", "topics", NULL };
  static KjNode* topicsP = kjNavigate(configTree, path, NULL, NULL);

  if (topicsP == NULL)
    KT_RE(NULL, "the field dds/ngsild/topic not found in DDS config file (looking for attribute '%s' of entity '%s')", attributeShortName, entityId);

  for (KjNode* topicP = topicsP->value.firstChildP; topicP != NULL; topicP = topicP->next)
  {
    KjNode* attrNodeP = kjLookup(topicP, "attribute");
    KjNode* entityIdP = kjLookup(topicP, "entityId");

    if ((entityIdP != NULL) && (strcmp(entityIdP->value.s, entityId) == 0))
    {
      if ((attrNodeP != NULL) && (strcmp(attrNodeP->value.s, attributeShortName) == 0))
        return topicP->name;
    }
  }

  return NULL;
}
