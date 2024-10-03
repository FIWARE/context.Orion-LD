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
#include "kjson/kjParse.h"                                  // kjParse
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjObject, kjChildAdd
}

#include "orionld/common/orionldState.h"                    // orionldState, kjTreeLog
#include "orionld/common/traceLevels.h"                     // KT_T trace levels
#include "orionld/common/tenantList.h"                      // tenant0
#include "orionld/context/orionldContextItemExpand.h"       // orionldContextItemExpand
#include "orionld/serviceRoutines/orionldPutAttribute.h"    // orionldPutAttribute
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsConfigTopicToAttribute.h"          // ddsConfigTopicToAttribute
#include "orionld/dds/ddsNotification.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ddsNotification -
//
void ddsNotification(const char* typeName, const char* topicName, const char* json, double publishTime)
{
  KT_T(StDdsNotification, "Got a notification on %s:%s (json: %s)", typeName, topicName, json);

  orionldStateInit(NULL);

  KjNode* kTree = kjParse(orionldState.kjsonP, (char*) json);
  if (kTree == NULL)
    KT_RVE("Error parsing json payload from DDS: '%s'", json);

  char* entityId      = NULL;
  char* entityType    = NULL;
  char* attrShortName = ddsConfigTopicToAttribute(topicName, &entityId, &entityType);

  if (attrShortName == NULL)
  {
    KT_W("Topic '%s' not found in the config file - redirect to default DDS entity", topicName);
    entityId      = (char*) "urn:ngsi-ld:dds:default";
    entityType    = (char*) "DDS";
    attrShortName = (char*) topicName;
  }

  KjNode* participantIdNodeP  = kjLookup(kTree, "id");
  if (participantIdNodeP != NULL)
  {
    char* pipe = strchr(participantIdNodeP->value.s, '|');
    if (pipe != NULL)
      *pipe = 0;
    participantIdNodeP->name = (char*) "participantId";
  }

  KjNode* idNodeP   = kjString(orionldState.kjsonP, "id", entityId);
  KjNode* typeNodeP = kjLookup(kTree, "type");

  if (typeNodeP != NULL)
    orionldState.ddsType = typeNodeP->value.s;

  char* expandedType = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);
  typeNodeP = kjString(orionldState.kjsonP, "type", expandedType);

  KjNode* attrValueNodeP = kjLookup(kTree, topicName);
  if (attrValueNodeP == NULL)
    KT_RVE("No attribute field ('%s') in DDS payload", topicName);

  orionldState.payloadIdNode   = idNodeP;
  orionldState.payloadTypeNode = typeNodeP;

  KjNode* attrNodeP = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(attrNodeP, attrValueNodeP);
  attrValueNodeP->name = (char*) "value";

  orionldState.requestTree         = attrNodeP;
  orionldState.uriParams.format    = (char*) "simplified";
  orionldState.uriParams.type      = typeNodeP->value.s;
  orionldState.wildcard[0]         = entityId;
  orionldState.wildcard[1]         = (char*) attrShortName;

  orionldState.tenantP             = &tenant0;  // FIXME ... Use tenants?
  orionldState.in.pathAttrExpanded = (char*) topicName;
  orionldState.ddsSample           = true;
  orionldState.ddsPublishTime      = publishTime;

  kjChildAdd(attrNodeP, participantIdNodeP);

  KjNode* publishedAt = kjFloat(orionldState.kjsonP, "publishedAt", publishTime);
  kjChildAdd(attrNodeP, publishedAt);

  //
  // If the entity does not exist, it needs to be created
  // Except of course, if it is registered and exists elsewhere
  //
  orionldPutAttribute();
}
