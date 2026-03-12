/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjNavigate.h"                               // kjNavigate
}

#include "orionld/common/traceLevels.h"                     // Trace levels for KTrace
#include "orionld/common/orionldState.h"                    // configTree
#include "orionld/dds/ddsActionCreate.h"                    // ddsActionCreate
#include "orionld/dds/ddsActionLookup.h"                    // ddsActionLookup
#include "orionld/dds/ddsServiceNotification.h"             // ddsEntityAttributeUpsert
#include "orionld/dds/ddsActionNotification.h"              // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionNotification -
//
// Called by the DDS Enabler when an action server is discovered on the network.
// Same pattern as ddsServiceNotification: lookup config, create entity/attribute.
//
void ddsActionNotification(const char* actionName, const eprosima::ddsenabler::participants::ActionInfo& actionInfo)
{
  KT_T(StDdsAction, "Got an Action Notification (actionName: %s)", actionName);

  DdsAction* actionP = ddsActionLookup(actionName);
  if (actionP != NULL)
  {
    KT_T(StDdsAction, "Action '%s' already exists", actionName);
    return;
  }

  //
  // Lookup action in config file: dds.ngsild.actions.<actionName>
  //
  char*       entityId      = (char*) "urn:ngsi-ld:dds:default";
  char*       entityType    = (char*) "DDS";
  char*       attributeName = (char*) actionName;
  const char* compV[5]      = { "dds", "ngsild", "actions", actionName, NULL };
  KjNode*     aNodeP        = kjNavigate(configTree, compV, NULL, NULL);

  if (aNodeP != NULL)
  {
    KT_T(StDdsAction, "Found action '%s' in config file", actionName);
    KjNode* eIdNodeP   = kjLookup(aNodeP, "entityId");
    KjNode* eTypeNodeP = kjLookup(aNodeP, "entityType");
    KjNode* attrNodeP  = kjLookup(aNodeP, "attribute");

    if (eIdNodeP   != NULL)    entityId      = eIdNodeP->value.s;
    if (eTypeNodeP != NULL)    entityType    = eTypeNodeP->value.s;
    if (attrNodeP  != NULL)    attributeName = attrNodeP->value.s;
  }
  else
    KT_T(StDdsAction, "Action '%s' not found in config file, using defaults", actionName);

  ddsActionCreate(actionName, entityId, entityType, attributeName);

  KT_T(StDdsAction, "Adding attribute '%s' to entity '%s' (type '%s') in DB", attributeName, entityId, entityType);
  ddsEntityAttributeUpsert(entityId, entityType, attributeName);
}
