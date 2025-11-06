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
#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjBuilder.h"                                // kjString, kjObject, kjChildAdd, ...
#include "kjson/kjLookup.h"                                 // kjLookup
}

#include "orionld/common/traceLevels.h"                     // Trace levels for KTrace
#include "orionld/common/orionldState.h"                    // orionldStateInit
#include "orionld/common/tenantList.h"                      // tenant0
#include "orionld/context/orionldCoreContext.h"             // orionldCoreContext
#include "orionld/context/orionldAttributeExpand.h"         // orionldAttributeExpand
#include "orionld/context/orionldContextItemExpand.h"       // orionldContextItemExpand
#include "orionld/mongoc/mongocEntityGet.h"                 // mongocEntityGet
#include "orionld/kjTree/kjNavigate.h"                      // kjNavigate
#include "orionld/serviceRoutines/orionldPostEntities.h"    // orionldPostEntities
#include "orionld/serviceRoutines/orionldPostEntity.h"      // orionldPostEntity
#include "orionld/serviceRoutines/orionldPutAttribute.h"    // orionldPutAttribute
#include "orionld/service/serviceLookupByServiceRoutine.h"  // serviceLookupByServiceRoutine
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsServiceCreate.h"                   // ddsServiceCreate, ddsServiceInfoAdd
#include "orionld/dds/ddsServiceLookup.h"                   // ddsServiceLookup



// -----------------------------------------------------------------------------
//
// ddsEntity -
//
static KjNode* ddsEntity(const char* entityId, const char* entityType, const char* attributeName)
{
  char*   typeLongName = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);
  KjNode* eP           = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrP        = kjObject(orionldState.kjsonP, attributeName);
  KjNode* attrTypeP    = kjString(orionldState.kjsonP, "type", "Property");
  KjNode* attrValueP   = kjString(orionldState.kjsonP, "value", "not initialized");

  orionldState.payloadIdNode   = kjString(orionldState.kjsonP, "id", entityId);
  orionldState.payloadTypeNode = kjString(orionldState.kjsonP, "type", typeLongName);

  kjChildAdd(eP, attrP);
  kjChildAdd(attrP, attrTypeP);
  kjChildAdd(attrP, attrValueP);

  return eP;
}



// -----------------------------------------------------------------------------
//
// ddsAttribute -
//
static KjNode* ddsAttribute(const char* attributeName)
{
  KjNode* body         = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrP        = kjObject(orionldState.kjsonP, attributeName);
  KjNode* attrTypeP    = kjString(orionldState.kjsonP, "type", "Property");
  KjNode* attrValueP   = kjString(orionldState.kjsonP, "value", "not initialized");

  kjChildAdd(body, attrP);
  kjChildAdd(attrP, attrTypeP);
  kjChildAdd(attrP, attrValueP);

  return body;
}



// -----------------------------------------------------------------------------
//
// ddsAttributeValues -
//
static KjNode* ddsAttributeValues(void)
{
  KjNode* body         = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrTypeP    = kjString(orionldState.kjsonP, "type", "Property");
  KjNode* attrValueP   = kjString(orionldState.kjsonP, "value", "not initialized");

  kjChildAdd(body, attrTypeP);
  kjChildAdd(body, attrValueP);

  return body;
}



// -----------------------------------------------------------------------------
//
// ddsEntityAttributeUpsert -
//
void ddsEntityAttributeUpsert(const char* entityId, const char* entityType, const char* attributeName)
{
  // 01. Initialize, so orionldState+kjson lib can be used
  orionldStateInit(NULL);
  orionldState.tenantP         = &tenant0;
  orionldState.contextP        = orionldCoreContextP;
  orionldState.ddsSample       = true;
  orionldState.uriParams.local = true;  // For now, all DDS data is "local only"

  // 02. Does the entity exist?
  KT_T(StDdsService, "Getting entity '%s' from mongo", entityId);
  KjNode* dbEntity = mongocEntityGet(entityId, NULL);
  KT_T(StDdsService, "Got entity '%s' from mongo: %p", entityId, dbEntity);

  // 03. If the entity exists, add the attribute
  //     If not, add the entire entity
  if (dbEntity == NULL)
  {
    KT_T(StDdsServicePrepopulate, "The entity '%s' doesn't exist - creating it", entityId);
    orionldState.serviceP    = serviceLookupByServiceRoutine(orionldPostEntities, HTTP_POST);
    orionldState.requestTree = ddsEntity(entityId, entityType, attributeName);

    KT_T(StDdsServicePrepopulate, "Calling orionldPostEntities");
    orionldPostEntities();
  }
  else
  {
    KT_T(StDdsServicePrepopulate, "The entity '%s' exists - what about the attribute '%s'?", entityId, attributeName);
    kjTreeLog2(dbEntity, "DB Entity", StDdsServicePrepopulate);

    char*       attrLongName  = orionldAttributeExpand(orionldState.contextP, attributeName, true, NULL);
    const char* compV[5]      = { "attrNames", attrLongName, NULL };
    KjNode*     aP            = kjNavigate(configTree, compV, NULL, NULL);

    orionldState.wildcard[0] = (char*) entityId;

    //
    // Lookup attribute 'attributeName'
    // If already exists, overwrite (PUT Attribute)
    // If not, POST /entities/entityId/attrs
    //

    if (aP == NULL)
    {
      orionldState.serviceP    = serviceLookupByServiceRoutine(orionldPostEntity, HTTP_POST);
      orionldState.requestTree = ddsAttribute(attributeName);

      KT_T(StDdsServicePrepopulate, "The attribute '%s' doesn't exist - alling orionldPostEntity", attributeName);
      orionldPostEntity();
    }
    else
    {
      orionldState.wildcard[1]         = (char*) attributeName;
      orionldState.in.pathAttrExpanded = attrLongName;
      orionldState.serviceP            = serviceLookupByServiceRoutine(orionldPutAttribute, HTTP_PUT);
      orionldState.requestTree         = ddsAttributeValues();

      KT_T(StDdsServicePrepopulate, "The attribute '%s' exists - calling orionldPutAttribute", attributeName);
      orionldPutAttribute();
    }
  }
}


// -----------------------------------------------------------------------------
//
// ddsServiceNotification -
//
void ddsServiceNotification(const char* serviceName, const eprosima::ddsenabler::participants::ServiceInfo& serviceInfo)
{
  KT_T(StDdsService, "Got a Service Notification (serviceName: %s)", serviceName);
  KT_T(StDdsService, "- requestTopic.type: '%s'", serviceInfo.request.type_name.c_str());
  KT_T(StDdsService, "- requestTopic.qos:  '%s'", serviceInfo.request.serialized_qos.c_str());
  KT_T(StDdsService, "- replyTopic.type:   '%s'", serviceInfo.reply.type_name.c_str());
  KT_T(StDdsService, "- replyTopic.qos:    '%s'", serviceInfo.reply.serialized_qos.c_str());

  //
  // Create the service unless it already exists
  //
  DdsService* serviceP = ddsServiceLookup(serviceName);
  if (serviceP == NULL)
  {
    //
    //   Lookup $serviceName in the config file - get attribute name, entity id and entity type.
    //   Then create the entity/attribute in the DB if need be.
    //   If $serviceName is not found in the coinfig file, create the attribute $serviceName in the default entity.
    //
    char*       entityId      = (char*) "urn:ngsi-ld:dds:default";
    char*       entityType    = (char*) "DDS";
    char*       attributeName = (char*) serviceName;
    const char* compV[5]      = { "dds", "ngsild", "services", serviceName, NULL };
    KjNode*     sNodeP        = kjNavigate(configTree, compV, NULL, NULL);

    if (sNodeP != NULL)
    {
      KT_T(StDdsService, "KZ: Found service '%s' in config file", serviceName);
      KjNode* eIdNodeP   = kjLookup(sNodeP, "entityId");
      KjNode* eTypeNodeP = kjLookup(sNodeP, "entityType");
      KjNode* attrNodeP  = kjLookup(sNodeP, "attribute");

      if (eIdNodeP   != NULL)    entityId      = eIdNodeP->value.s;
      if (eTypeNodeP != NULL)    entityType    = eTypeNodeP->value.s;
      if (attrNodeP  != NULL)    attributeName = attrNodeP->value.s;

      ddsServiceCreate(serviceName,
                       serviceInfo.request.type_name.c_str(),
                       serviceInfo.request.serialized_qos.c_str(),
                       serviceInfo.reply.type_name.c_str(),
                       serviceInfo.reply.serialized_qos.c_str(),
                       entityId,
                       entityType,
                       attributeName);
    }
    else
    {
      KT_T(StDdsService, "KZ: Did not find service '%s' in config file", serviceName);
      ddsServiceCreate(serviceName,
                       serviceInfo.request.type_name.c_str(),
                       serviceInfo.request.serialized_qos.c_str(),
                       serviceInfo.reply.type_name.c_str(),
                       serviceInfo.reply.serialized_qos.c_str(),
                       NULL,
                       NULL,
                       NULL);
    }

    KT_T(StDdsService, "KZ: Adding attribute '%s' to entity '%s' (type '%s') in DB", attributeName, entityId, entityType);
    ddsEntityAttributeUpsert(entityId, entityType, attributeName);
  }
  else
    ddsServiceInfoAdd(serviceP,
                     serviceInfo.request.type_name.c_str(),
                     serviceInfo.request.serialized_qos.c_str(),
                     serviceInfo.reply.type_name.c_str(),
                     serviceInfo.reply.serialized_qos.c_str());
}
