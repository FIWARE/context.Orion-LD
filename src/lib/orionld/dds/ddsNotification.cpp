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
#include "orionld/types/OrionLdRestService.h"               // OrionLdRestService, OrionLdRestServiceVector, OrionldServiceRoutine
#include "orionld/config/configDdsTopicToAttribute.h"       // configDdsTopicToAttribute
#include "orionld/service/orionldServiceInit.h"             // orionldRestServiceV
#include "orionld/serviceRoutines/orionldPutAttribute.h"    // orionldPutAttribute
#include "orionld/mongoc/mongocConnectionRelease.h"         // mongocConnectionRelease
#include "orionld/context/orionldContextItemExpand.h"       // orionldContextItemExpand
#include "orionld/notifications/orionldAlterationsTreat.h"  // orionldAlterationsTreat
#include "orionld/service/serviceLookupByServiceRoutine.h"  // serviceLookupByServiceRoutine
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsNotification.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// ddsNotification -
//
void ddsNotification(const char* topicName, const char* json, int64_t publishTime)
{
  KT_T(StDdsNotification, "----------------------------------------");
  KT_T(StDdsNotification, "Got a notification on topic %s (json: %s)", topicName, json);

  orionldStateInit(NULL);

  KjNode* kTree = kjParse(orionldState.kjsonP, (char*) json);
  if (kTree == NULL)
    KT_RVE("Error parsing json payload from DDS: '%s'", json);

  char* entityId      = NULL;
  char* entityType    = NULL;
  char* attrShortName = configDdsTopicToAttribute(topicName, &entityId, &entityType);

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

  KjNode* topicNameNodeP = kjLookup(kTree, topicName);
  if (topicNameNodeP == NULL)
    KT_RVE("No attribute field ('%s') in DDS payload", topicName);
  KjNode* dataNodeP = kjLookup(topicNameNodeP, "data");
  if (dataNodeP == NULL)
    KT_RVE("No 'data' field in DDS attribute payload", topicName);

  KjNode* tNodeP = kjLookup(topicNameNodeP, "type");
  if (tNodeP != NULL)
    kjChildRemove(topicNameNodeP, tNodeP);

  KjNode* valueNodeP  = dataNodeP->value.firstChildP;
  char*   xId         = valueNodeP->name;
  KjNode* subAttrP    = kjString(orionldState.kjsonP, "xId", xId);
  KjNode* attrNodeP   = kjObject(orionldState.kjsonP, NULL);

  valueNodeP->name = (char*) "value";

  kjChildAdd(attrNodeP, valueNodeP);

  // Add the xId node as "hidden" sub-property - not to be included in GETs, only for DDS publish reconstruction
  kjChildAdd(attrNodeP, subAttrP);

  orionldState.payloadIdNode   = idNodeP;
  orionldState.payloadTypeNode = typeNodeP;

  orionldState.requestTree         = attrNodeP;
  orionldState.requestTree->name   = orionldContextItemExpand(orionldState.contextP, attrShortName, true, NULL);

  orionldState.uriParams.format    = (char*) "simplified";
  orionldState.uriParams.type      = typeNodeP->value.s;
  orionldState.wildcard[0]         = entityId;
  orionldState.wildcard[1]         = (char*) attrShortName;

  orionldState.tenantP             = &tenant0;  // FIXME ... Use tenants?
  orionldState.in.pathAttrExpanded = orionldState.requestTree->name;
  orionldState.ddsSample           = true;
  orionldState.ddsPublishTime      = publishTime;
  orionldState.apiVersion          = API_VERSION_NGSILD_V1;

  kjChildAdd(attrNodeP, participantIdNodeP);

  if (tNodeP != NULL)
  {
    KjNode* dataType    = kjString(orionldState.kjsonP, "ddsDataType", tNodeP->value.s);
    kjChildAdd(attrNodeP, dataType);
  }

  KjNode* publishedAt = kjInteger(orionldState.kjsonP, "publishedAt", publishTime);
  kjChildAdd(attrNodeP, publishedAt);

  orionldState.serviceP = serviceLookupByServiceRoutine(orionldPutAttribute, HTTP_PUT);

  orionldPutAttribute();

  //
  // Cleanup
  //
  void* con_cls;
  extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);

  requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);
}
