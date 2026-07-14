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
*/
#include <string.h>                                            // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kalloc/kaAlloc.h"                                    // kaAlloc
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjArray, kjObject, kjChildAdd
#include "kjson/kjClone.h"                                     // kjClone
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldError.h"                       // orionldError
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/common/tenantList.h"                         // tenant0
#include "orionld/common/entitySuccessPush.h"                  // entitySuccessPush
#include "orionld/common/entityErrorPush.h"                    // entityErrorPush
#include "orionld/common/entityLookupById.h"                   // entityLookupBy_id_Id
#include "orionld/common/batchEntityCountAndFirstCheck.h"      // batchEntityCountAndFirstCheck
#include "orionld/common/batchEntityStringArrayPopulate.h"     // batchEntityStringArrayPopulate
#include "orionld/common/batchEntitiesFinalCheck.h"            // batchEntitiesFinalCheck
#include "orionld/common/batchMultipleInstances.h"             // batchMultipleInstances
#include "orionld/common/batchUpdateEntity.h"                  // batchUpdateEntity
#include "orionld/common/batchCreateEntity.h"                  // batchCreateEntity
#include "orionld/types/OrionldAlteration.h"                   // OrionldAlteration
#include "orionld/types/StringArray.h"                         // StringArray
#include "orionld/dbModel/dbModelToApiEntity.h"                // dbModelToApiEntity
#include "orionld/mongoc/mongocEntitiesQuery.h"                // mongocEntitiesQuery
#include "orionld/mongoc/mongocEntitiesUpsert.h"               // mongocEntitiesUpsert
#include "orionld/notifications/alteration.h"                  // alteration
#include "orionld/notifications/previousValues.h"              // previousValues
#include "orionld/service/serviceLookupByServiceRoutine.h"     // serviceLookupByServiceRoutine
#include "orionld/serviceRoutines/orionldPostBatchUpsert.h"    // orionldPostBatchUpsert
#include "orionld/types/OrionldMimeType.h"                     // MT_JSONLD, MT_JSON
#include "orionld/kafka/kafkaBatchProcess.h"                   // Own interface


extern bool  troe;
extern bool  experimental;



// -----------------------------------------------------------------------------
//
// kafkaBatchProcess - process a batch of NGSI-LD entities from Kafka
//
// Follows the same pipeline as orionldPostBatchUpsert() but without HTTP
// response construction. The entities go through:
//   1. 3-round validation (batchEntityCountAndFirstCheck, batchEntityStringArrayPopulate, batchEntitiesFinalCheck)
//   2. MongoDB current state upsert
//   3. TRoE temporal writes (via serviceP->troeRoutine in requestCompleted)
//   4. Notification dispatch (via alterations in requestCompleted)
//
bool kafkaBatchProcess(KjNode* entityArray)
{
  //
  // Set up orionldState for batch upsert processing
  //
  orionldState.requestTree   = entityArray;
  orionldState.tenantP       = &tenant0;
  orionldState.apiVersion    = API_VERSION_NGSILD_V1;

  // Use update semantics (append attributes to existing entities)
  orionldState.uriParamOptions.update = true;

  // Content-Type: NGSI-LD over Kafka carries its @context inline in each entity (application/ld+json).
  // The REST path sets orionldState.in.contentType from the HTTP Content-Type, which makes
  // batchEntitiesFinalCheck() resolve every entity's @context and expand its terms against it. The Kafka
  // path never set it, so contentType stayed at the core-context default and the inline @context was
  // ignored (terms expanded against the core/default context). Mirror REST: when the batch carries an
  // inline @context, flag it as JSON-LD so the existing per-entity @context handling kicks in.
  KjNode* firstEntityP       = (entityArray != NULL) ? entityArray->value.firstChildP : NULL;
  orionldState.in.contentType = ((firstEntityP != NULL) && (kjLookup(firstEntityP, "@context") != NULL))
                                ? MT_JSONLD
                                : MT_JSON;

  // Look up the batch upsert service to link TRoE routine
  orionldState.serviceP = serviceLookupByServiceRoutine(orionldPostBatchUpsert, HTTP_POST);

  //
  // Round 1: Basic structural checks
  //
  KjNode*  outArrayErroredP = kjArray(orionldState.kjsonP, "errors");
  int      noOfEntities     = batchEntityCountAndFirstCheck(entityArray, outArrayErroredP);

  if (noOfEntities == 0)
  {
    KT_T(KtKafka, "kafkaBatchProcess: no valid entities after 1st check round");
    return true;  // Not an error - just nothing to process
  }

  //
  // Round 2: Extract entity IDs
  //
  StringArray  eIdArray;

  eIdArray.items = noOfEntities;
  eIdArray.array = (char**) kaAlloc(&orionldState.kalloc, sizeof(char*) * noOfEntities);

  if (eIdArray.array == NULL)
  {
    KT_E("kafkaBatchProcess: out of memory allocating StringArray");
    return false;
  }

  noOfEntities = batchEntityStringArrayPopulate(entityArray, &eIdArray, outArrayErroredP, false);

  if (noOfEntities == 0)
  {
    KT_T(KtKafka, "kafkaBatchProcess: no valid entities after 2nd check round");
    return true;
  }

  //
  // Query MongoDB for existing entities
  //
  orionldState.uriParams.limit  = noOfEntities;
  orionldState.uriParams.offset = 0;

  KjNode* dbEntityArray = mongocEntitiesQuery(NULL, &eIdArray, NULL, NULL, NULL, NULL, NULL, NULL, NULL, orionldState.uriParams.orderBy);
  if (dbEntityArray == NULL)
  {
    KT_E("kafkaBatchProcess: database query for entities failed");
    return false;
  }

  //
  // Round 3: Full validation with DB context
  //
  noOfEntities = batchEntitiesFinalCheck(entityArray, outArrayErroredP, dbEntityArray, true, false, false);

  if (noOfEntities == 0)
  {
    KT_T(KtKafka, "kafkaBatchProcess: no valid entities after 3rd check round");
    return true;
  }

  //
  // Process validated entities - create vs update
  //
  KjNode* dbCreateArray  = kjArray(orionldState.kjsonP, NULL);
  KjNode* dbUpdateArray  = kjArray(orionldState.kjsonP, NULL);
  KjNode* outArraySuccessP = kjArray(orionldState.kjsonP, "success");

  KjNode* next;
  KjNode* inEntityP = entityArray->value.firstChildP;

  while (inEntityP != NULL)
  {
    next = inEntityP->next;

    KjNode*  idNodeP            = kjLookup(inEntityP, "id");
    KjNode*  typeNodeP          = kjLookup(inEntityP, "type");
    char*    entityId           = idNodeP->value.s;
    char*    entityType         = (typeNodeP != NULL) ? typeNodeP->value.s : NULL;
    KjNode*  originalDbEntityP  = entityLookupBy_id_Id(dbEntityArray, entityId, NULL);
    KjNode*  finalDbEntityP;
    bool     multipleEntities   = false;

    if (batchMultipleInstances(entityId, outArraySuccessP, outArraySuccessP) == true)
    {
      multipleEntities = true;

      KjNode* dbArray = (originalDbEntityP == NULL) ? dbCreateArray : dbUpdateArray;
      KjNode* dbArrayItemP = entityLookupBy_id_Id(dbArray, entityId, NULL);
      if (dbArrayItemP != NULL)
        kjChildRemove(dbArray, dbArrayItemP);

      if (originalDbEntityP == NULL)
        originalDbEntityP = dbArrayItemP;
      else
        originalDbEntityP = dbArrayItemP ? dbArrayItemP : originalDbEntityP;
    }

    if (originalDbEntityP == NULL)
    {
      // Entity does not exist - CREATE
      finalDbEntityP = batchCreateEntity(inEntityP, entityId, entityType, multipleEntities);

      if (finalDbEntityP != NULL)
        kjChildAdd(dbCreateArray, finalDbEntityP);
    }
    else
    {
      // Entity exists - UPDATE (append attributes)
      KjNode* dbAttrsP = kjLookup(originalDbEntityP, "attrs");

      previousValues(inEntityP, dbAttrsP);
      finalDbEntityP = batchUpdateEntity(inEntityP, originalDbEntityP, false);

      if (finalDbEntityP != NULL)
        kjChildAdd(dbUpdateArray, finalDbEntityP);
    }

    if (finalDbEntityP == NULL)
    {
      inEntityP = next;
      continue;
    }

    //
    // Build alteration for notifications
    //
    KjNode* dbEntityCopy    = kjClone(orionldState.kjsonP, finalDbEntityP);
    KjNode* finalApiEntityP = dbModelToApiEntity(dbEntityCopy, false, entityId);

    alteration(entityId, entityType, finalApiEntityP, inEntityP, NULL);

    inEntityP = next;
  }

  //
  // Write to MongoDB
  //
  if ((dbCreateArray->value.firstChildP != NULL) || (dbUpdateArray->value.firstChildP != NULL))
  {
    int r = mongocEntitiesUpsert(dbCreateArray, dbUpdateArray);

    if (r == false)
    {
      KT_E("kafkaBatchProcess: mongocEntitiesUpsert failed");
      return false;
    }
  }

  KT_T(KtKafka, "kafkaBatchProcess: batch of %d entities processed successfully", noOfEntities);

  //
  // TRoE writes and notifications are handled by requestCompleted() in the caller,
  // which reads orionldState.alterations and calls serviceP->troeRoutine.
  //
  return true;
}
