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
extern "C"
{
#include "kjson/KjNode.h"                                     // KjNode
#include "kjson/kjLookup.h"                                   // kjLookup
#include "ktrace/kTrace.h"                                    // trace messages - ktrace library
}

#include "orionld/common/orionldState.h"                      // ddsConfigTree
#include "orionld/common/traceLevels.h"                       // KT_T trace levels
#include "orionld/kjTree/kjNavigate.h"                        // kjNavigate
#include "orionld/dds/kjTreeLog.h"                            // kjTreeLog2
#include "orionld/dds/ddsConfigTopicToAttribute.h"            // Own interface



// -----------------------------------------------------------------------------
//
// ddsConfigTree - "hidden" external variable
//
// It's the KjNode tree of the DDS config file
//
extern KjNode* ddsConfigTree;  // Better not to put this variable in any header file ...



// -----------------------------------------------------------------------------
//
// ddsConfigTopicToAttribute -
//
char* ddsConfigTopicToAttribute(const char* topic, char** entityIdPP, char** entityTypePP)
{
  if (ddsConfigTree == NULL)
    return NULL;  // No error - it's OK to not have a DDS Config File

  const char*    path[4] = { "ddsmodule", "ngsild", "topics", NULL };
  static KjNode* topicsP = kjNavigate(ddsConfigTree, path, NULL, NULL);
  KjNode*        topicP  = kjLookup(topicsP, topic);

  if (topicP == NULL)
    KT_RE(NULL, "topic '%s' not found in DDS config file", topic);

  KjNode* attributeP = kjLookup(topicP, "attribute");

  if (attributeP == NULL)
    KT_RE(NULL, "topic '%s' without 'attribute' member in DDS config file", topic);

  if (entityIdPP != NULL)
  {
    KjNode* entityIdNodeP = kjLookup(topicP, "entityId");
    *entityIdPP = (entityIdNodeP != NULL)? entityIdNodeP->value.s : NULL;
  }

  if (entityTypePP != NULL)
  {
    KjNode* entityTypeNodeP = kjLookup(topicP, "entityType");
    *entityTypePP = (entityTypeNodeP != NULL)? entityTypeNodeP->value.s : NULL;
  }

  return attributeP->value.s;
}
