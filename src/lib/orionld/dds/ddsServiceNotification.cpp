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
#include "orionld/mongoc/mongocEntityGet.h"                 // mongocEntityGet
#include "orionld/kjTree/kjNavigate.h"                      // kjNavigate
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsServiceCreate.h"                   // ddsServiceCreate
#include "orionld/dds/ddsServiceLookup.h"                   // ddsServiceLookup



// -----------------------------------------------------------------------------
//
// ddsEntityAttributeUpsert -
//
void ddsEntityAttributeUpsert(const char* entityId, const char* entityType, const char* attributeName)
{
  // 01. Initialize, so orionldState+kjson lib can be used
  orionldStateInit(NULL);
  orionldState.tenantP = &tenant0;

  // 02. Does the entity exist?
  KT_T(StDdsService, "Getting entity '%s' from mongo", entityId);
  KjNode* dbEntity = mongocEntityGet(entityId, NULL);
  KT_T(StDdsService, "Got entity '%s' from mongo: %p", entityId, dbEntity);

  // 03. If it exists, add the attribute
  if (dbEntity == NULL)
  {
    KT_T(StDdsService, "The entity '%s' doesn't exist - creating it", entityId);
  }
  else
  {
    KT_T(StDdsService, "The entity '%s' exists - what about the attribute '%s'?", entityId, attributeName);
    kjTreeLog2(dbEntity, "DB Entity", StDdsService);
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
  if (ddsServiceLookup(serviceName) == NULL)
  {
    ddsServiceCreate(serviceName,
                     serviceInfo.request.type_name.c_str(),
                     serviceInfo.request.serialized_qos.c_str(),
                     serviceInfo.reply.type_name.c_str(),
                     serviceInfo.reply.serialized_qos.c_str());

    //
    //   Lookup $serviceName in the config file - get attribute name, entity id and entity type.
    //   Then create the entity/attribute in the DB if need be.
    //   If $serviceName is not found in the coinfig file, create the attribute $serviceName in the default entity.
    //
    char*       entityId      = (char*) "urn:ngsi-ld:dds:default";
    char*       entityType    = (char*) "DDS";
    char*       attributeName = (char*) serviceName;
    const char* compV[5]      = { "dds", "ngsild", "services", serviceName, NULL };

    KjNode* sP = kjNavigate(configTree, compV, NULL, NULL);

    if (sP != NULL)
    {
      KT_T(StDdsService, "Found service '%s' in config file", serviceName);
      KjNode* eIdNodeP   = kjLookup(sP, "entityId");
      KjNode* eTypeNodeP = kjLookup(sP, "entityType");
      KjNode* attrNodeP  = kjLookup(sP, "attribute");

      if (eIdNodeP   != NULL)    entityId      = eIdNodeP->value.s;
      if (eTypeNodeP != NULL)    entityType    = eTypeNodeP->value.s;
      if (attrNodeP  != NULL)    attributeName = attrNodeP->value.s;
    }
    else
      KT_T(StDdsService, "Did not find service '%s' in config file", serviceName);
    
    KT_T(StDdsService, "Create entity '%s' (type '%s') with attribute '%s' to DB", entityId, entityType, attributeName);
    ddsEntityAttributeUpsert(entityId, entityType, attributeName);
  }
}
