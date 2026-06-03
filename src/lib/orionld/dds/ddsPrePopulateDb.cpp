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
#include "kjson/kjNavigate.h"                               // kjNavigate
#include "kjson/kjChildCount.h"                             // kjChildCount
#include "kjson/kjClone.h"                                  // kjClone
}

#include "orionld/types/StringArray.h"                      // StringArray
#include "orionld/common/orionldState.h"                    // orionldState
#include "orionld/common/traceLevels.h"                     // Trace levels for KTrace
#include "orionld/common/tenantList.h"                      // tenant0
#include "orionld/common/dotForEq.h"                        // dotForEq
#include "orionld/kjTree/kjTreeNavigate.h"                  // kjTreeNavigate
#include "orionld/kjTree/kjEntityIdLookupInEntityArray.h"   // kjEntityIdLookupInEntityArray
#include "orionld/context/orionldCoreContext.h"             // orionldCoreContextP
#include "orionld/context/orionldContextItemExpand.h"       // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"         // orionldAttributeExpand
#include "orionld/dbModel/dbModelFromApiEntity.h"           // dbModelFromApiEntity
#include "orionld/dbModel/dbModelFromApiAttribute.h"        // dbModelFromApiAttribute
#include "orionld/mongoc/mongocEntitiesQuery.h"             // mongocEntitiesQuery
#include "orionld/mongoc/mongocEntitiesUpsert.h"            // mongocEntitiesUpsert
#include "orionld/mongoc/mongocAttributesAdd.h"             // mongocAttributesAdd
#include "orionld/troe/troePostEntities.h"                  // troePostEntities
#include "orionld/dds/ddsPrePopulateDb.h"                   // DdsConceptType, DdsPrePopulateInput
#include "orionld/dds/ddsServiceLookup.h"                   // ddsServiceLookup
#include "orionld/dds/ddsServiceCreate.h"                   // ddsServiceCreate
#include "orionld/dds/ddsActionLookup.h"                    // ddsActionLookup
#include "orionld/dds/ddsActionCreate.h"                    // ddsActionCreate
#include "orionld/kjTree/kjTreeLog.h"                       // KT_TREE



extern bool troe;  // Is TRoE enabled? (defined in orionld.cpp)



// -----------------------------------------------------------------------------
//
// kjDbEntityLookupInArray -
//
static KjNode* kjDbEntityLookupInArray(KjNode* entityV, const char* entityId)
{
  for (KjNode* dbEntityP = entityV->value.firstChildP; dbEntityP != NULL; dbEntityP = dbEntityP->next)
  {
    KjNode* idNodeP = kjTreeNavigate(dbEntityP, "_id.id", NULL);
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
  strncpy(eqName, longAttrName, sizeof(eqName));

  const char* compV[3]  = { "attrs", eqName, NULL };
  KjNode*     attrNodeP = kjNavigate(dbEntityP, compV, NULL, NULL);

  return attrNodeP;
}



// -----------------------------------------------------------------------------
//
// DdsPrePopulateInput -
//
typedef struct DdsPrePopulateInput
{
  DdsConceptType  type;
  KjNode*         configNode;
} DdsPrePopulateInput;



// -----------------------------------------------------------------------------
//
// ddsPrePopulateDbInThread -
//
static void* ddsPrePopulateDbInThread(void* vP)
{
  DdsPrePopulateInput*  inputP      = (DdsPrePopulateInput*) vP;
  KjNode*               topics      = inputP->configNode;
  bool                  isService   = (inputP->type == DdsServices);
  bool                  isAction    = (inputP->type == DdsActions);
  const char*           conceptName;

  switch (inputP->type)
  {
  case DdsTopics:    conceptName = "topics";   break;
  case DdsServices:  conceptName = "services"; break;
  case DdsActions:   conceptName = "actions";  break;
  default:           conceptName = "unknown";  break;
  }

  // Allocate kjson - local to avoid races between threads
  char    kallocBuffer[2048];
  KAlloc  kaLocal;
  Kjson   kjLocal;

  orionldStateInit(NULL);

  bzero(kallocBuffer, sizeof(kallocBuffer));
  kaBufferInit(&kaLocal, kallocBuffer, sizeof(kallocBuffer), 8 * 1024, NULL, "ddsPrePopulateDb KAlloc buffer");
  orionldState.kjsonP = kjBufferCreate(&kjLocal, &kaLocal);
  orionldState.tenantP = &tenant0;

  KT_TREE(topics, conceptName, StDdsPrePopulate);
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
    KT_T(StDdsPrePopulate, "%s '%s'", conceptName, topic->name);
    KjNode* entityTypeNode  = kjLookup(topic, "entityType");
    KjNode* entityIdNode    = kjLookup(topic, "entityId");
    KjNode* attrNameNode    = kjLookup(topic, "attribute");
    char*   entityType      = (entityTypeNode  != NULL)? entityTypeNode->value.s  : NULL;
    char*   entityId        = (entityIdNode    != NULL)? entityIdNode->value.s    : NULL;
    char*   attrName        = (attrNameNode    != NULL)? attrNameNode->value.s    : NULL;

    if ((entityType == NULL) || (entityId == NULL) || (attrName == NULL))
    {
      KT_W("Topic/Service '%s' is incomplete in the config file (entityType: '%s', entityId: '%s', attribute: '%s')", topic->name, entityType, entityId, attrName);
      continue;
    }

    entityIds.array[entityIds.items] = entityId;
    ++entityIds.items;

    pickList.array[pickList.items] = entityId;
    ++pickList.items;

    if (isService == true)
    {
      if (ddsServiceLookup(topic->name) == NULL)
        ddsServiceCreate(topic->name, NULL, NULL, NULL, NULL, entityId, entityType, attrName);
    }
    else if (isAction == true)
    {
      if (ddsActionLookup(topic->name) == NULL)
        ddsActionCreate(topic->name, entityId, entityType, attrName);
    }
  }

  KT_T(StDdsPrePopulate, "-------------------------------------------------------------");


  // Query mongo
  KjNode* entityV = mongocEntitiesQuery(NULL, &entityIds, NULL, NULL, &pickList, NULL, NULL, NULL, NULL, NULL);

  if (entityV != NULL)
    KT_TREE(entityV, "entityV", StDdsPrePopulate);

  //
  // Now we know what's in the config file and what's in the DB.
  // Any entity/attribute in the config file that does not exist is to be created (with "empty" values
  //
  KjNode* dbCreateV = kjArray(orionldState.kjsonP, NULL);

  //
  // TRoE mirror: when TRoE is enabled, the entities/attributes we pre-create must also be
  // recorded in Postgres - not only in MongoDB. troeCreateV keeps the API-form copies (id,
  // type, attrs) that troePostEntities() needs; dbCreateV holds the DB-model copies for mongo.
  //
  KjNode* troeCreateV = (troe == true)? kjArray(orionldState.kjsonP, NULL) : NULL;

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

    KT_T(StDdsPrePopulate, "Entity '%s'. Attribute '%s'", entityId, attrName);

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

        KT_T(StDdsPrePopulate, "Entity '%s' EXISTS, Attribute '%s' %s", entityId, longAttrName, (attributeExists == true)? "EXISTS" : "DOESN'T EXIST");
      }
    }

    //
    // Must also look in the dbCreateV - might be there are more than one attribute for the same entity in the config file
    //
    KjNode* preEntityP = NULL;
    if (entityExists == false)
    {
      preEntityP = kjDbEntityLookupInArray(dbCreateV, entityId);
      if (preEntityP != NULL)
      {
        entityExists = true;
        KT_T(StDdsPrePopulate, "Found entity '%s' in dbCreateV array", entityId);
      }
      else
        KT_T(StDdsPrePopulate, "Did not find entity '%s' in dbCreateV array", entityId);
    }

    //
    // Create or Update (or nothing) - depending on entityExists and attributeExists
    //
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

      // Keep an API-form copy for TRoE before dbModelFromApiEntity destructively rewrites 'entity'
      if (troeCreateV != NULL)
        kjChildAdd(troeCreateV, kjClone(orionldState.kjsonP, entity));

      dbModelFromApiEntity(entity, NULL, true, entityId, entityType);
      kjChildAdd(dbCreateV, entity);
      KT_T(StDdsPrePopulate, "Added entity '%s' to dbCreateV array", entityId);
      // KT_TREE(dbCreateV, "dbCreateV", StDdsPrePopulate);
    }
    else if (attributeExists == false)  // Add the attribute to existing entity
    {
      if (preEntityP == NULL)  // Meaning, the entity exists in the DB - call mongocAttributesAdd directly
      {
        KT_T(StDdsPrePopulate, "Attribute '%s' of '%s' doesn't exist", attrName, entityId);
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
      else
      {
        KT_T(StDdsPrePopulate, "Adding attribute '%s' to Pre-Entity '%s'", attrName, entityId);
        KjNode* attribute      = kjObject(orionldState.kjsonP, longAttrName);
        KjNode* attrType       = kjString(orionldState.kjsonP, "type", "Property");
        KjNode* attrValue      = kjString(orionldState.kjsonP, "value", "uninitialized");

        kjChildAdd(attribute, attrType);
        kjChildAdd(attribute, attrValue);

        dbModelFromApiAttribute(attribute, NULL, NULL, NULL, NULL, NULL, NULL);
        KjNode* attrsP = kjLookup(preEntityP, "attrs");

        kjChildAdd(attrsP, attribute);

        // Mirror the attribute into the API-form TRoE copy of the same pre-entity
        if (troeCreateV != NULL)
        {
          KjNode* troeEntityP = kjEntityIdLookupInEntityArray(troeCreateV, entityId);
          if (troeEntityP != NULL)
          {
            KjNode* apiAttr  = kjObject(orionldState.kjsonP, longAttrName);
            KjNode* apiAType = kjString(orionldState.kjsonP, "type", "Property");
            KjNode* apiAVal  = kjString(orionldState.kjsonP, "value", "uninitialized");

            kjChildAdd(apiAttr, apiAType);
            kjChildAdd(apiAttr, apiAVal);
            kjChildAdd(troeEntityP, apiAttr);
          }
        }
      }
    }
  }

  if (dbCreateV->value.firstChildP != NULL)
  {
    // KT_TREE(dbCreateV, "dbCreateV", StDdsPrePopulate);
    mongocEntitiesUpsert(dbCreateV, NULL);
  }

  //
  // Mirror the freshly pre-created entities into TRoE (Postgres). Without this, the entity
  // has a row in MongoDB (current state) but none in the Postgres 'entities' table, so the
  // broker can't reconstruct it for temporal queries even though later attribute updates do
  // land in the 'attributes' table.
  //
  if ((troeCreateV != NULL) && (troeCreateV->value.firstChildP != NULL))
  {
    orionldState.troeOpMode = TROE_ENTITY_CREATE;

    for (KjNode* troeEntityP = troeCreateV->value.firstChildP; troeEntityP != NULL; troeEntityP = troeEntityP->next)
    {
      KjNode* idP   = kjLookup(troeEntityP, "id");
      KjNode* typeP = kjLookup(troeEntityP, "type");

      if ((idP == NULL) || (typeP == NULL))
        continue;

      //
      // troePostEntities expects requestTree to hold attributes ONLY (id/type removed), with
      // payloadIdNode/payloadTypeNode pointing at the now-detached id/type nodes.
      //
      kjChildRemove(troeEntityP, idP);
      kjChildRemove(troeEntityP, typeP);

      orionldState.requestTree     = troeEntityP;
      orionldState.payloadIdNode   = idP;
      orionldState.payloadTypeNode = typeP;

      troePostEntities();
    }
  }

  free(entityIds.array);
  free(pickList.array);
  orionldStateRelease();
  kaBufferReset(&kaLocal, true);
  free(inputP);
  pthread_exit(0);

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsPrePopulateDb -
//
void ddsPrePopulateDb(DdsConceptType type, KjNode* configNode)
{
  DdsPrePopulateInput* inputP = (DdsPrePopulateInput*) malloc(sizeof(DdsPrePopulateInput));

  inputP->type       = type;
  inputP->configNode = configNode;

  pthread_t tid;
  pthread_create(&tid, NULL, ddsPrePopulateDbInThread, (void*) inputP);
}
