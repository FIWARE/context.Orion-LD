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
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjChildRemove, ...
}

#include "orionld/common/orionldState.h"                    // orionldState, kjTreeLog
#include "orionld/common/traceLevels.h"                     // KT_T trace levels
#include "orionld/common/tenantList.h"                      // tenant0
#include "orionld/serviceRoutines/orionldPutAttribute.h"    // orionldPutAttribute
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsConfigTopicToAttribute.h"          // ddsConfigTopicToAttribute
#include "orionld/dds/ddsNotification.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ddsNotification -
//
void ddsNotification(const char* entityType, const char* entityId, const char* topicName, KjNode* notificationP)
{
  KT_V("Got a notification from DDS");
  kjTreeLog2(notificationP, "notification", StDds);

  //
  // We receive entire NGSI-LD Attributes
  //
  KjNode* aValueNodeP = kjLookup(notificationP, "attributeValue");

  if (entityId == NULL)
  {
    KT_W("Entity without id from DDS");
    return;
  }

  //
  // Criteria for obtaining the necessary attribute info (Entity ID+Type + Attribute long name):
  //
  //   1. Set the attribute long name to the topic name
  //   2. Take all three from the DDS config file (depending on the topic name)
  //   3. Override entity id+type with entityType+entityId from the parameters of this function
  //

  //
  // GET the attribute long name (and entity id+type) from the DDS config file
  //
  char* eId               = NULL;
  char* eType             = NULL;
  char* attributeLongName = ddsConfigTopicToAttribute(topicName, &eId, &eType);

  if (attributeLongName == NULL)  // Topic name NOT found in DDS config file
    attributeLongName = (char*) topicName;

  // Take entity id+type from config file unless given as parameters to this function (which would override)
  if (entityType == NULL)
    entityType = eType;
  if (entityId == NULL)
    entityId = eId;

  // What to do if we have no entity id+type ?
  // - The entity id is MANDATORY - cannot continue if we don't know the entity ID
  // - The entity type is onbly needed when creating the entity - and we don't know right now whether the entity already exists.
  //     So, we let it pass and get an error later (404 Not Found)
  if (entityId == NULL)
  {
    KT_E(("Got a DDS sample for an entity whose ID cannot be obtained"));
    return;
  }

  orionldState.uriParams.type      = (char*) entityType;

  orionldState.wildcard[0]         = (char*) entityId;
  orionldState.wildcard[1]         = (char*) attributeLongName;  // The topic is the attribute long name

  orionldState.requestTree         = aValueNodeP;
  orionldState.tenantP             = &tenant0;  // FIXME ... Use tenants?
  orionldState.in.pathAttrExpanded = (char*) topicName;
  orionldState.ddsSample           = true;

  KT_T(StDds, "Calling orionldPutAttribute");

  //
  // If the entity does not exist, it needs to be created
  // Except of course, if it is registered and exists elsewhere
  //
  orionldPutAttribute();
}
