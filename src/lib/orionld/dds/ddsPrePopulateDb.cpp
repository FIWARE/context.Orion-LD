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
#include <pthread.h>                                        // pthread_create, ...

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kalloc/kaBufferInit.h"                            // kaBufferInit
#include "kalloc/kaBufferReset.h"                           // kaBufferReset
#include "kjson/kjBufferCreate.h"                           // kjBufferCreate
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjObject, kjArray, kjString, ...
}

#include "orionld/types/StringArray.h"                      // StringArray
#include "orionld/common/orionldState.h"                    // orionldState
#include "orionld/common/traceLevels.h"                     // Trace levels for KTrace
#include "orionld/common/tenantList.h"                      // tenant0
#include "orionld/common/dotForEq.h"                        // dotForEq
#include "orionld/config/configInit.h"                      // configTree
#include "orionld/kjTree/kjNavigate.h"                      // kjNavigate2
#include "orionld/kjTree/kjChildCount.h"                    // kjChildCount
#include "orionld/context/orionldCoreContext.h"             // orionldCoreContextP
#include "orionld/context/orionldContextItemExpand.h"       // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"         // orionldAttributeExpand
#include "orionld/dbModel/dbModelFromApiEntity.h"           // dbModelFromApiEntity
#include "orionld/dbModel/dbModelFromApiAttribute.h"        // dbModelFromApiAttribute
#include "orionld/mongoc/mongocEntitiesQuery.h"             // mongocEntitiesQuery
#include "orionld/mongoc/mongocEntitiesUpsert.h"            // mongocEntitiesUpsert
#include "orionld/mongoc/mongocAttributesAdd.h"             // mongocAttributesAdd
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2



// -----------------------------------------------------------------------------
//
// kjDbEntityLookupInArray -
//
static KjNode* kjDbEntityLookupInArray(KjNode* entityV, const char* entityId)
{
  for (KjNode* dbEntityP = entityV->value.firstChildP; dbEntityP != NULL; dbEntityP = dbEntityP->next)
  {
    KjNode* idNodeP = kjNavigate2(dbEntityP, "_id.id", NULL);
    char*   eId     = (idNodeP != NULL)? idNodeP->value.s : NULL;

    if ((eId != NULL) && (strcmp(eId, entityId) == 0))
      return dbEntityP;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// kjDbAttrLookupInDbEntity -
//
static KjNode* kjDbAttrLookupInDbEntity(KjNode* dbEntityP, const char* longAttrName)
{
  char eqName[512];
  strncpy(eqName, longAttrName, sizeof(eqName) -1);

  const char* compV[3]  = { "attrs", eqName, NULL };
  KjNode*     attrNodeP = kjNavigate(dbEntityP, compV, NULL, NULL);

  return attrNodeP;
}



// -----------------------------------------------------------------------------
//
// ddsPrePopulateDbInThread -
//
static void* ddsPrePopulateDbInThread(void* vP)
{
  // Allocate kjson
  char kallocBuffer[2048];

  orionldStateInit(NULL);

  bzero(kallocBuffer, sizeof(kallocBuffer));
  kaBufferInit(&kalloc, kallocBuffer, sizeof(kallocBuffer), 8 * 1024, NULL, "ddsPrePopulateDb KAlloc buffer");
  orionldState.kjsonP = kjBufferCreate(&kjson, &kalloc);
  orionldState.tenantP = &tenant0;

  KjNode* topics = kjNavigate2(configTree, "dds.ngsild.topics", NULL);

  if (topics == NULL)
  {
    KT_W("No DDS Topics for NGSILD (dds.ngsild.topics) in the config file ...");
    return NULL;
  }

  kjTreeLog2(topics, "topics", StDdsPrePopulate);
  KT_T(StDdsPrePopulate, "-------------------------------------------------------------");

  int         entities = kjChildCount(topics);
  StringArray entityIds;
  StringArray pickList;

  entityIds.items = 0;  // Increments in the following loop
  entityIds.array = (char**) malloc((entities + 1) * sizeof(char*));

  pickList.items = 2;  // Increments in the following loop, but first adding 'id' and 'type'
  pickList.array = (char**) malloc((entities + 3) * sizeof(char*));

  pickList.array[0] = (char*) "id";
  pickList.array[1] = (char*) "type";
  
  for (KjNode* topic = topics->value.firstChildP; topic != NULL; topic = topic->next)
  {
    KT_T(StDdsPrePopulate, "Topic '%s'", topic->name);
    KjNode* entityTypeNode = kjLookup(topic, "entityType");
    KjNode* entityIdNode   = kjLookup(topic, "entityId");
    KjNode* attrNameNode   = kjLookup(topic, "attribute");
    char*   entityType     = (entityTypeNode != NULL)? entityTypeNode->value.s : NULL;
    char*   entityId       = (entityIdNode   != NULL)? entityIdNode->value.s   : NULL;
    char*   attrName       = (attrNameNode   != NULL)? attrNameNode->value.s   : NULL;

    if ((entityType == NULL) || (entityId == NULL) || (attrName == NULL))
    {
      KT_W("Topic '%s' is incomplete in the config file (entityType: '%s', entityId: '%s', attribute: '%s')", topic->name, entityType, entityId, attrName);
      continue;
    }

    entityIds.array[entityIds.items] = entityId;
    ++entityIds.items;

    pickList.array[pickList.items] = entityId;
    ++pickList.items;
  }

  KT_T(StDdsPrePopulate, "-------------------------------------------------------------");


  // Query mongo
  KjNode* entityV = mongocEntitiesQuery(NULL, &entityIds, NULL, NULL, &pickList, NULL, NULL, NULL, NULL, NULL);

  if (entityV != NULL)
    kjTreeLog2(entityV, "entityV", StDdsPrePopulate);

  //
  // Now we know what's in the config file and what's in the DB.
  // Any entity/attribute in the config file that does not exist is to be created (with "empty" values
  //
  KjNode* dbCreateV = kjArray(orionldState.kjsonP, NULL);

  for (KjNode* topic = topics->value.firstChildP; topic != NULL; topic = topic->next)
  {
    KjNode* entityTypeNode = kjLookup(topic, "entityType");
    KjNode* entityIdNode   = kjLookup(topic, "entityId");
    KjNode* attrNameNode   = kjLookup(topic, "attribute");
    char*   entityType     = (entityTypeNode != NULL)? entityTypeNode->value.s : NULL;
    char*   entityId       = (entityIdNode   != NULL)? entityIdNode->value.s   : NULL;
    char*   attrName       = (attrNameNode   != NULL)? attrNameNode->value.s   : NULL;

    if ((entityType == NULL) || (entityId == NULL) || (attrName == NULL))
    {
      KT_W("Topic '%s' is incomplete in the config file (entityType: '%s', entityId: '%s', attribute: '%s')", topic->name, entityType, entityId, attrName);
      continue;
    }

    char*  longEntityType  = orionldContextItemExpand(orionldCoreContextP, entityType, true, NULL);
    char*  longAttrName    = orionldAttributeExpand(orionldCoreContextP, attrName, true, NULL);
    bool   entityExists    = false;  // Initialized to false, assuming the entity does not already exist in the brokers DB
    bool   attributeExists = false;  // same same

    char eqName[512];
    strncpy(eqName, longAttrName, sizeof(eqName) - 1);
    dotForEq(eqName);

    // Checking whether the entity and attribute already exist
    if (entityV != NULL)
    {
      KjNode* dbEntityP = kjDbEntityLookupInArray(entityV, entityId);

      if (dbEntityP != NULL)
      {
        entityExists = true;
        if (kjDbAttrLookupInDbEntity(dbEntityP, eqName) != NULL)
          attributeExists = true;

        KT_T(StDdsPrePopulate, "Entity '%s' EXISTS, Attribute '%s' $s", entityId, longAttrName);
      }
    }

    // Create or Update (or nothing) - depending on entityExists and attributeExists
    if (entityExists == false)
    {
      KjNode* entity         = kjObject(orionldState.kjsonP, NULL);
      KjNode* id             = kjString(orionldState.kjsonP, "id", entityId);
      KjNode* type           = kjString(orionldState.kjsonP, "type", longEntityType);
      KjNode* attribute      = kjObject(orionldState.kjsonP, longAttrName);
      KjNode* attrType       = kjString(orionldState.kjsonP, "type", "Property");
      KjNode* attrValue      = kjString(orionldState.kjsonP, "value", "uninitialized");

      kjChildAdd(entity, id);
      kjChildAdd(entity, type);
      kjChildAdd(entity, attribute);

      kjChildAdd(attribute, attrType);
      kjChildAdd(attribute, attrValue);

      // Adding an attribute 'ddsType'
      attribute = kjObject(orionldState.kjsonP, "ddsType");
      attrType  = kjString(orionldState.kjsonP, "type", "Property");
      attrValue = kjString(orionldState.kjsonP, "value", "fastdds");

      kjChildAdd(attribute, attrType);
      kjChildAdd(attribute, attrValue);

      kjChildAdd(entity, attribute);

      dbModelFromApiEntity(entity, NULL, true, entityId, entityType);
      kjChildAdd(dbCreateV, entity);
    }
    else if (attributeExists == false) // Add the attribute to existing entity
    {
      KjNode* newDbAttrNamesV = kjArray(orionldState.kjsonP, NULL);
      KjNode* newDbAttrName   = kjString(orionldState.kjsonP, NULL, eqName);
      KjNode* attribute       = kjObject(orionldState.kjsonP, longAttrName);
      KjNode* attrType        = kjString(orionldState.kjsonP, "type", "Property");
      KjNode* attrValue       = kjString(orionldState.kjsonP, "value", "uninitialized");
      bool    r;

      kjChildAdd(attribute, attrType);
      kjChildAdd(attribute, attrValue);

      kjChildAdd(newDbAttrNamesV, newDbAttrName);

      dbModelFromApiAttribute(attribute, NULL, NULL, NULL, NULL, NULL, NULL);
      r = mongocAttributesAdd(entityId, newDbAttrNamesV, attribute, true);
      if (r != true)
        KT_E("Error adding attribute '%s' to '%s'", attribute->name, entityId);
    }
  }

  if (dbCreateV->value.firstChildP != NULL)
  {
    kjTreeLog2(dbCreateV, "dbCreateV", StDdsPrePopulate);
    mongocEntitiesUpsert(dbCreateV, NULL);
  }

  orionldStateRelease();
  kaBufferReset(&orionldState.kalloc, true);
  pthread_exit(0);

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsPrePopulateDb -
//
void ddsPrePopulateDb(void)
{
  pthread_t tid;

  pthread_create(&tid, NULL, ddsPrePopulateDbInThread, NULL);
}
