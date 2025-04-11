/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildRemove
#include "kjson/kjClone.h"                                       // kjClone
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/common/dateTime.h"                             // dateTimeFromString
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup
#include "orionld/mongoc/mongocEntityReplace.h"                  // mongocEntityReplace
#include "orionld/mongoc/mongocEntityInsert.h"                   // mongocEntityInsert
#include "orionld/dbModel/dbModelFromApiEntity.h"                // dbModelFromApiEntity
#include "orionld/dbModel/dbModelToApiEntity.h"                  // dbModelToApiEntity2
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/payloadCheck/pCheckUri.h"                      // pCheckUri
#include "orionld/payloadCheck/pCheckEntityType.h"               // pCheckEntityType
#include "orionld/payloadCheck/pCheckEntity.h"                   // pCheckEntity
#include "orionld/notifications/previousValues.h"                // previousValues
#include "orionld/serviceRoutines/orionldPutEntity.h"            // Own Interface



// ----------------------------------------------------------------------------
//
// entityIdCheck -
//
static bool entityIdCheck(KjNode* idP, const char* entityIdFromUrl)
{
  if (idP != NULL)
  {
    if (idP->type != KjString)
    {
      orionldError(OrionldBadRequestData, "Invalid JSON type", "id", 400);
      return false;
    }

    if (strcmp(idP->value.s, entityIdFromUrl) != 0)
    {
      orionldError(OrionldBadRequestData, "Non-matching entity id in payload body", idP->value.s, 400);
      return false;
    }
  }

  if (pCheckUri(entityIdFromUrl, "id", true) == false)  // This can never happen!
    return false;

  return true;
}



// ----------------------------------------------------------------------------
//
// orionldPutEntity -
//
bool orionldPutEntity(void)
{
  if ((experimental == false) || (orionldState.in.legacy != NULL))
  {
    orionldError(OrionldResourceNotFound, "Service Not Found", orionldState.urlPath, 404);
    return false;
  }

  if (orionldState.requestTree->type != KjObject)
  {
    orionldError(OrionldResourceNotFound, "Invalid payload body", "Invalid JSON type for Entity - not a JSON Object", 400);
    return false;
  }

  if (orionldState.requestTree->value.firstChildP == NULL)
  {
    orionldError(OrionldResourceNotFound, "Invalid payload body", "Empty JSON Object for Entity", 400);
    return false;
  }

  char* entityId = orionldState.wildcard[0];

  //
  // Check Entity ID - if entity id is present in payload body, it must be identical to the entity id in the URL PATH
  //
  KjNode* idNodeP = kjLookup(orionldState.requestTree, "id");
  if (entityIdCheck(idNodeP, entityId) == false)
    return false;

  //
  // Check Entity Type
  //
  char*   entityType = orionldState.uriParams.type;  // Set by pCheckEntityType - need to look at that ...
  KjNode* typeNodeP  = kjLookup(orionldState.requestTree, "type");

  if (typeNodeP != NULL)
    orionldState.payloadTypeNode = typeNodeP;

  if (pCheckEntityType(typeNodeP, true, &entityType) == false)
    return false;

  orionldState.entityTypeForTroe = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);

  //
  // Get the entity from the database
  //
  KjNode* oldDbEntityP = mongocEntityLookup(entityId, NULL, NULL, NULL, NULL);

  if ((oldDbEntityP == NULL) && (orionldState.upsert == false))
  {
    orionldError(OrionldResourceNotFound, "Entity does not exist", entityId, 404);
    return false;
  }

  // The Entity Type cannot be altered
  KjNode*  _idP             = kjLookup(oldDbEntityP, "_id");
  KjNode* dbTypeP           = (_idP    != NULL)? kjLookup(_idP, "type") : NULL;
  char*   entityTypeFromDb  = (dbTypeP != NULL)? dbTypeP->value.s       : NULL;

  if (entityTypeFromDb == NULL)
  {
    orionldError(OrionldInternalError, "Database Error", "Entity without type in database", 500);
    pdEntityId(entityId);
    return false;
  }

  if (strcmp(orionldState.entityTypeForTroe, entityTypeFromDb) != 0)
  {
    orionldError(OrionldBadRequestData, "Inconsistent Entity Type", "Entity Type cannot be altered", 400);
    pdEntityId(entityId);
    pdOldValue(entityTypeFromDb);
    pdNewValue(orionldState.entityTypeForTroe);
  }

  //
  // Check the attributes
  //
  KjNode* dbAttrsP = (oldDbEntityP != NULL)? kjLookup(oldDbEntityP, "attrs") : NULL;
  if (pCheckEntity(orionldState.requestTree, false, dbAttrsP) == false)
    return false;

  if (dbAttrsP != NULL)
    previousValues(orionldState.requestTree, dbAttrsP);

  //
  // Create the new DB Entity, based mainly on orionldState.requestTree
  // From oldDbEntityP (old db content) we just keep the creDate of the entity
  //
  // FIXME: Use dbModelFromApiEntity instead of apiEntityToDbEntity
  //
  // KjNode* dbEntityP = apiEntityToDbEntity(orionldState.requestTree, oldDbEntityP, entityId, entityType);
  //

  // Get the entity type from the DB ?
  KjNode* dbEntityP = kjClone(orionldState.kjsonP, orionldState.requestTree);
  if (dbModelFromApiEntity(dbEntityP, dbEntityP, true, entityId, entityType) == false)
  {
    if (orionldState.pd.type == 0)  // Not filled in - let's fill it in
      orionldError(OrionldInternalError, "Internal Error", "unable to transform API enmtity to DB model", 500);

    return false;
  }

  if (oldDbEntityP != NULL)
  {
    if (orionldState.datasets != NULL)
      kjChildAdd(dbEntityP, orionldState.datasets);

    if (mongocEntityReplace(dbEntityP, entityId) == false)
    {
      orionldError(OrionldInternalError, "Database Error", "mongocEntityReplace failed", 500);
      return false;
    }
  }
  else  // Create the entity
  {
    if (mongocEntityInsert(dbEntityP, entityId) == false)
    {
      orionldError(OrionldInternalError, "Database Error", "mongocEntityInsert failed", 500);
      return false;
    }
  }

  KjNode* finalApiEntityWithSysAttrs = dbModelToApiEntity2(dbEntityP, true, RF_NORMALIZED, NULL, false, &orionldState.pd);

  orionldState.alterations = (OrionldAlteration*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAlteration));
  orionldState.alterations->entityId                    = entityId;
  orionldState.alterations->entityType                  = typeNodeP->value.s;
  orionldState.alterations->inEntityP                   = orionldState.requestTree;
  orionldState.alterations->dbEntityP                   = NULL;
  orionldState.alterations->finalApiEntityWithSysAttrsP = finalApiEntityWithSysAttrs;
  orionldState.alterations->finalApiEntityP             = orionldState.requestTree;
  orionldState.alterations->alteredAttributes           = 0;
  orionldState.alterations->alteredAttributeV           = NULL;
  orionldState.alterations->next                        = NULL;

  //
  // Is the entity id inside orionldState.requestTree?
  // If not, it must be added for the notification
  //
  idNodeP = kjLookup(orionldState.requestTree, "id");
  if (idNodeP == NULL)
  {
    idNodeP = kjString(orionldState.kjsonP, "id", entityId);
    kjChildAdd(orionldState.requestTree, idNodeP);
  }

  orionldState.httpStatusCode = 204;

  return true;
}
