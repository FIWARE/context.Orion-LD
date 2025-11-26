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
extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjBuilder.h"                                // kjObject, kjChildAdd
}

#include "ddsenabler_participants/Callbacks.hpp"            // eprosima::ddsenabler::participants::TopicInfo

#include "orionld/types/OrionLdRestService.h"               // OrionLdRestService, OrionLdRestServiceVector, OrionldServiceRoutine
#include "orionld/common/traceLevels.h"                     // Trace levels for KTrace
#include "orionld/common/orionldState.h"                    // orionldState
#include "orionld/common/tenantList.h"                      // tenant0
#include "orionld/context/orionldContextItemExpand.h"       // orionldContextItemExpand
#include "orionld/service/orionldServiceInit.h"             // orionldRestServiceV
#include "orionld/service/serviceLookupByServiceRoutine.h"  // serviceLookupByServiceRoutine
#include "orionld/serviceRoutines/orionldPatchAttribute.h"  // orionldPatchAttribute
#include "orionld/config/configDdsTopicToAttribute.h"       // configDdsTopicToAttribute



// -----------------------------------------------------------------------------
//
// ddsTopicNotification -
//
void ddsTopicNotification(const char* topicName, const eprosima::ddsenabler::participants::TopicInfo& topicInfo)
{
  char* entityId      = NULL;
  char* entityType    = NULL;
  char* attrShortName = configDdsTopicToAttribute(topicName, &entityId, &entityType);
  char* topicType     = (char*) topicInfo.type_name.c_str();

  KT_T(StDds, "Got a topic notification (topic: '%s', type: '%s', qos: '%s')", topicName, topicType, topicInfo.serialized_qos.c_str());

  if (attrShortName == NULL)
  {
    KT_W("Topic '%s' not found in the config file - not updating anything", topicName);
    return;
  }

  KT_T(StDds, "Add sub-attribute 'ddsTypeName': '%s', to attribute '%s' of entity '%s'", topicType, attrShortName, entityId);
  orionldStateInit(NULL);

  KjNode* payloadBody = kjObject(orionldState.kjsonP, NULL);
  KjNode* ddsTypeName = kjString(orionldState.kjsonP, "ddsTypeName", topicType);

  kjChildAdd(payloadBody, ddsTypeName);

  orionldState.requestTree         = payloadBody;
  orionldState.requestTree->name   = orionldContextItemExpand(orionldState.contextP, attrShortName, true, NULL);

  orionldState.uriParams.format    = (char*) "simplified";
  orionldState.wildcard[0]         = entityId;
  orionldState.wildcard[1]         = (char*) attrShortName;
  orionldState.tenantP             = &tenant0;  // FIXME ... Use tenants?
  orionldState.in.pathAttrExpanded = orionldState.requestTree->name;
  orionldState.ddsSample           = true;
  orionldState.apiVersion          = API_VERSION_NGSILD_V1;

  orionldState.serviceP = serviceLookupByServiceRoutine(orionldPatchAttribute, HTTP_PUT);

  orionldPatchAttribute();

  //
  // Cleanup
  //
  void* con_cls;
  extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);

  requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);

#if 0
  DdsType* typeP = ddsTypeLookup(typeName);

  if (typeP != NULL)
    typeP->topic = strdup(topicName);

  ddsTypeList();
#endif
}
