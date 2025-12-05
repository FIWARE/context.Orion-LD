/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject
#include "kjson/kjChildReplace.h"                                // kjChildReplace
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjClone.h"                                       // kjClone
}

#include "orionld/types/OrionldAttributeType.h"                  // OrionldAttributeType, orionldAttributeType
#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/OrionldResponseErrorType.h"              // OrionldResponseErrorType
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/responseFix.h"                          // responseFix
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/httpStatusCodeToOrionldErrorType.h"     // httpStatusCodeToOrionldErrorType
#include "orionld/common/numberToDate.h"                         // numberToDate
#include "orionld/kjTree/kjTreeLog.h"                            // KT_TREE
#include "orionld/payloadCheck/pCheckUri.h"                      // pCheckUri
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup
#include "orionld/mongoc/mongocAttributeReplace.h"               // mongocAttributeReplace
#include "orionld/mongoc/mongocEntityFieldReplace.h"             // mongocEntityFieldReplace
#include "orionld/payloadCheck/pCheckAttribute.h"                // pCheckAttribute
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/dbModel/dbModelToApiEntity.h"                  // dbModelToApiEntity2
#include "orionld/dbModel/dbModelFromApiAttribute.h"             // dbModelFromApiAttribute
#include "orionld/dbModel/dbModelAttributeCreatedAtLookup.h"     // dbModelAttributeCreatedAtLookup
#include "orionld/dbModel/dbModelAttributePublishedAtLookup.h"   // dbModelAttributePublishedAtLookup
#include "orionld/dbModel/dbModelAttributeCreatedAtSet.h"        // dbModelAttributeCreatedAtSet
#include "orionld/dbModel/dbModelAttributeLookup.h"              // dbModelAttributeLookup
#include "orionld/dbModel/dbModelEntityTypeLookup.h"             // dbModelEntityTypeLookup
#include "orionld/regMatch/regMatchForEntityGet.h"               // regMatchForEntityGet
#include "orionld/distOp/distOpSend.h"                           // distOpSend
#include "orionld/distOp/distOpRequests.h"                       // distOpRequests
#include "orionld/distOp/distOpResponses.h"                      // distOpResponses
#include "orionld/distOp/distOpListRelease.h"                    // distOpListRelease
#include "orionld/distOp/distOpFailure.h"                        // distOpFailure
#include "orionld/distOp/distOpSuccess.h"                        // distOpSuccess
#include "orionld/distOp/distOpListsMerge.h"                     // distOpListsMerge
#include "orionld/distOp/xForwardedForCompose.h"                 // xForwardedForCompose
#include "orionld/distOp/viaCompose.h"                           // viaCompose
#include "orionld/dds/ddsEntityCreateFromAttribute.h"            // ddsEntityCreateFromAttribute
#include "orionld/dds/ddsAttributeCreate.h"                      // ddsAttributeCreate
#include "orionld/dds/ddsPublishAttribute.h"                     // ddsPublishAttribute
#include "orionld/notifications/alteration.h"                    // alteration
#include "orionld/notifications/previousValuePopulate.h"         // previousValuePopulate
#include "orionld/notifications/sysAttrsStrip.h"                 // sysAttrsStrip
#include "orionld/serviceRoutines/orionldPutAttribute.h"         // Own Interface



// ----------------------------------------------------------------------------
//
// entityTypeSelect -
//
static const char* entityTypeSelect(const char* entityId, const char* entityTypeFromUriParam, KjNode* dbEntityP, bool* entityTypeMismatchP)
{
  if (dbEntityP != NULL)
  {
    char* entityTypeFromDb = dbModelEntityTypeLookup(dbEntityP, entityId);

    if (entityTypeFromDb == NULL)
      KT_W("Entity '%s' has no type in the database!!!", entityId);
    else
    {
      if (entityTypeFromUriParam != NULL)
      {
        if (strcmp(entityTypeFromUriParam, entityTypeFromDb) != 0)
        {
          KT_W("Entity Type via URI Parameter (%s) differs fronm the one in the database (%s)", entityTypeFromUriParam, entityTypeFromDb);
          KT_W("Multi Type is not yet supported. Picking the entity type from the URI Parameter");
          *entityTypeMismatchP = true;
          return entityTypeFromUriParam;
        }
      }

      return entityTypeFromDb;
    }
  }

  return entityTypeFromUriParam;
}



// -----------------------------------------------------------------------------
//
// datasetInstanceReplace -
//
static void datasetInstanceReplace(KjNode* dbAttrDatasetV, KjNode* oldInstanceP, KjNode* newInstanceP)
{
  KT_TREE(oldInstanceP, "old db dataset instance", KtSR);
  KT_TREE(newInstanceP, "new db dataset instance", KtSR);

  if (dbAttrDatasetV->type == KjObject)
  {
    // Replace entity object (all children)
    dbAttrDatasetV->value.firstChildP = newInstanceP->value.firstChildP;
    dbAttrDatasetV->lastChild         = newInstanceP->lastChild;
  }
  else
    kjChildReplace(dbAttrDatasetV, oldInstanceP, newInstanceP);
}



// ----------------------------------------------------------------------------
//
// entityMergeInAttribute -
//
static void entityMergeInAttribute(KjNode* apiEntityP, KjNode* newAttrP)
{
  KjNode* oldAttrP = kjLookup(apiEntityP, newAttrP->name);

  KT_T(KtSR, "Old attribute '%s' at: %p", newAttrP->name, oldAttrP);
  if (oldAttrP != NULL)
  {
    KT_TREE(oldAttrP, "OLD", KtSR);
    KT_TREE(newAttrP, "NEW", KtSR);

    kjChildRemove(apiEntityP, oldAttrP);
    kjChildAdd(apiEntityP, newAttrP);
  }
}



// ----------------------------------------------------------------------------
//
// sysAttrs -
//
static void sysAttrs(KjNode* container, double createdAt, double modifiedAt)
{
  char* createdAtV  = kaAlloc(&orionldState.kalloc, 32);
  char* modifiedAtV = kaAlloc(&orionldState.kalloc, 32);

  if (createdAt == 0)
    createdAt = modifiedAt;

  numberToDate(createdAt,  createdAtV,  32);
  numberToDate(modifiedAt, modifiedAtV, 32);

  KjNode* createdAtP  = kjString(orionldState.kjsonP, "createdAt", createdAtV);
  KjNode* modifiedAtP = kjString(orionldState.kjsonP, "modifiedAt", modifiedAtV);

  kjChildAdd(orionldState.requestTree, createdAtP);
  kjChildAdd(orionldState.requestTree, modifiedAtP);
}



// ----------------------------------------------------------------------------
//
// orionldPutAttribute -
//
// REIMPLEMENT for datasetId support:
// 1. Get "dbEntityP" from mongo (we need the Entity Type for DistOps)
// 2. Send all distOps - save results (204 and 404 are especially interesting)
//    - if any distOp error != 404, use that one as error and stop
// 3. If the attribute has been chopped off by Exclusive/Redirect registrations, we're done
// 4. Get the entity from mongo, inclusing "@datasets", if datasetId is in use
// 5. Lookup the attribute in the DB Entity
// 6. if datasetId is present - lookup the DB field @datasets.<attrLongNameEq>.[datasetId match]
// 7. Check for 404
//    - if dbEntityP == NULL || dbAttrP == NULL:
//    - If any 204 in distOp responses, return 204
//    - [ before we checked for any distOp error != 204 and != 404 ]
//    - [ so, now we're in a situation with ALL (or none) distOps returned 404 ]
//    - If no datasetId and "Entity not found" => 404 Entity Not Found
//      - Actually, should check 404's in distOps for all Attribute Not Found and use that if so)
//
bool orionldPutAttribute(void)
{
  char*   entityId               = orionldState.wildcard[0];
  char*   attrName               = orionldState.wildcard[1];
  char*   entityTypeFromUriParam = orionldState.uriParams.type;       // Is it already expanded?
  char*   attrLongName           = orionldState.in.pathAttrExpanded;

  // Make sure the Entity ID (from URI variable) is a valid URI
  if (pCheckUri(entityId, "Entity ID from URL PATH", true) == false)
    return false;

  // Make sure the Entity ID (from URI parameter) is a valid URI or a shortname
  if ((entityTypeFromUriParam != NULL) && pCheckUri(entityTypeFromUriParam, "Entity Type from URL Parameter", false) == false)
    return false;

  // Get the Entity from the database (if it's there ...)
  char*       detail             = NULL;
  KjNode*     dbEntityP          = mongocEntityLookup(entityId, NULL, NULL, NULL, &detail);

  if (dbEntityP != NULL)
    KT_TREE(dbEntityP, "dbEntity", KtSR);

  //
  // Is a DDS notification the source of this update?
  // If so, and if the entity doesn't exist, redirect to different servide routine (oprionldPostEntities)
  //
  if ((orionldState.ddsSample == true) && (dbEntityP == NULL))
    return ddsEntityCreateFromAttribute(orionldState.requestTree, entityId, attrName);

  // Select what entity type to use (from URI param, from DB, or NULL)
  bool        entityTypeMismatch = false;
  const char* entityType         = entityTypeSelect(entityId, entityTypeFromUriParam, dbEntityP, &entityTypeMismatch);

  //
  // What to do if the user provides an Entity Type, but an Entity with a matching Entity ID has a different Entity Type?
  // For now, I treat that as a not-found, as I don't support multi-typing.
  //
  if (entityTypeMismatch == true)
    dbEntityP = NULL;

  orionldState.entityTypeForTroe = (char*) entityType;

  KT_T(KtSR, "In orionldPutAttribute: entity type:  '%s'", (entityType != NULL)? entityType : "Not Known");
  KT_T(KtSR, "In orionldPutAttribute: entity id:    '%s'", entityId);
  KT_T(KtSR, "In orionldPutAttribute: attrName:     '%s'", attrName);
  KT_T(KtSR, "In orionldPutAttribute: attrLongName: '%s'", attrLongName);

  //
  // DistOps
  //
  DistOp* distOpList  = NULL;
  int     distOps404s = 0;
  int     distOps204s = 0;
  int     distOps     = 0;
  DistOp* otherP      = NULL;

  if ((orionldState.distributed == true) && (orionldState.uriParams.local == false))
  {
    KT_T(KtDistOpRequest, "Distributed - checking reg matches");
    KjNode* entityObject = kjObject(orionldState.kjsonP, NULL);
    KjNode* attrClone    = kjClone(orionldState.kjsonP, orionldState.requestTree);

    attrClone->name = attrLongName;
    kjChildAdd(entityObject, attrClone);
    distOpList = distOpRequests(entityId, (char*) entityType, DoReplaceAttr, entityObject);
  }

  KT_T(KtDistOpRequest, "distOpList at %p", distOpList);
  if (distOpList != NULL)
  {
    for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
    {
      KT_T(KtDistOpRequest, "Got a DistOp response of %d", distOpP->httpResponseCode);
      ++distOps;

      if (distOpP->httpResponseCode == 204)
        distOps204s += 1;
      else if (distOpP->httpResponseCode == 404)
        distOps404s += 1;
      else
      {
        KT_T(KtDistOpRequest, "Other Error: %d", distOpP->httpResponseCode);
        otherP = distOpP;
      }
    }
  }

  if (orionldState.attributeConsumed == true)
  {
    KT_T(KtDistOpRequest, "The attribute has been consumed by Exclusive/Redirect registration");
    if (distOps204s > 0)
      orionldState.httpStatusCode = 204;
    else if (distOps404s == distOps)
      orionldError(OrionldResourceNotFound, "Entity/Attribute Not Found", entityId, 404);
    else
    {
      if (otherP->errorType == 0)
        otherP->errorType = httpStatusCodeToOrionldErrorType(otherP->httpResponseCode);

      orionldError(otherP->errorType, otherP->title, otherP->detail, otherP->httpResponseCode);

      //
      // Add the registration ID and the attribute
      //
      orionldState.pd.registrationId = otherP->regP->regId;
      orionldState.pd.attribute      = attrLongName;

      return false;
    }

    // Nothing done in local => no TRoE, no Notifications
    return true;
  }

  //
  // datasetId?
  //
  KT_TREE(orionldState.requestTree, "Incoming", KtSR);
  KjNode*     datasetIdNodeP = kjLookup(orionldState.requestTree, "datasetId");
  const char* datasetId      = (datasetIdNodeP != NULL)?datasetIdNodeP->value.s : NULL;
  KjNode*     dbAttrDatasetP = NULL;
  KjNode*     dbAttrDatasetV = NULL;
  char*       attrLongNameEq = kaStrdup(&orionldState.kalloc, attrLongName);

  dotForEq(attrLongNameEq);

  if (datasetId != NULL)
  {
    KT_T(KtSR, "datasetId: '%s'", datasetId);
    KjNode* datasets = kjLookup(dbEntityP, "@datasets");

    dbAttrDatasetV = (datasets != NULL)? kjLookup(datasets, attrLongNameEq) : NULL;
    if (dbAttrDatasetV != NULL)
    {
      for (KjNode* instanceP = dbAttrDatasetV->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
      {
        KjNode* datassetIdP = kjLookup(instanceP, "datasetId");

        if (datassetIdP == NULL)
          KT_W("DB Error - instance of attribute '%s' has no datasetId in @datasets field in DB", attrLongNameEq);
        else
        {
          if (strcmp(datassetIdP->value.s, datasetId) == 0)
          {
            dbAttrDatasetP = instanceP;
            break;
          }
        }
      }
    }
  }


  //
  // Default instance?
  //
  KjNode* dbAttrsP = NULL;
  KjNode* dbAttrP  = NULL;

  if (dbEntityP != NULL)
  {
    dbAttrsP = kjLookup(dbEntityP, "attrs");

    if (dbAttrsP != NULL)
      dbAttrP = kjLookup(dbAttrsP, attrLongNameEq);

    // DDS
    if (orionldState.ddsSample == true)  // Coming in from DDS
    {
      if (dbAttrP == NULL)
        return ddsAttributeCreate(orionldState.requestTree, entityType, attrName);
      else
      {
        int64_t publishedAt = dbModelAttributePublishedAtLookup(dbAttrP);
        if (publishedAt > orionldState.ddsPublishTime)
          return true;
      }
    }
  }

  // It's OK to modify the attribute type in a PUT Attribute operation (thus NoAttributeType)
  if (pCheckAttribute(entityId, orionldState.requestTree, true, NoAttributeType, true, NULL) == false)
    KT_RE(false, "pCheckAttribute failed");  // pCheckAttribute() calls orionldError

  previousValuePopulate(NULL, dbAttrP, orionldState.in.pathAttrExpanded);

  //
  // 404 ?
  //
  if ((distOpList == NULL) || (distOps404s == distOps))
  {
    if (dbEntityP == NULL)
      orionldError(OrionldResourceNotFound, "Entity Not Found", entityId, 404);

    if (datasetId == NULL)
    {
      if (dbAttrP == NULL)
        orionldError(OrionldResourceNotFound, "Attribute Not Found", attrName, 404);
    }
    else if ((datasetId != NULL) && (dbAttrDatasetP) == NULL)
      orionldError(OrionldResourceNotFound, "Attribute Dataset Instance Not Found", attrName, 404);
  }


  //
  // Local DB processing
  //
  double createdAt = 0;
  if (dbEntityP != NULL)
  {
    //
    // Need to keep the initial attribute (orionldState.requestTree) for notifications, TRoE, DDS
    // So, we close the payload to create thje DB modeled attribute
    //
    KjNode* dbAttributeP = kjClone(orionldState.kjsonP, orionldState.requestTree);

    // The attribute name needs to be in DB format (replace dots for '=')
    dbAttributeP->name = attrLongNameEq;

    bool  r      = false;
    char* detail = (char*) "all good";

    if (dbAttrDatasetP != NULL)  // dataset instance to be replaced
    {
      // Need the createdAt from the DB, as it must stay intact
      KjNode* createdAtP  = kjLookup(dbAttrDatasetP, "createdAt");

      createdAt   = (createdAtP != NULL)? createdAtP->value.f : 0;
      dbModelAttributeCreatedAtSet(dbAttributeP, createdAt, "createdAt");

      KjNode* modifiedAtP = kjFloat(orionldState.kjsonP, "modifiedAt", orionldState.requestTime);
      kjChildAdd(dbAttributeP, modifiedAtP);
      datasetInstanceReplace(dbAttrDatasetV, dbAttrDatasetP, dbAttributeP);

      char datasetPath[512];
      snprintf(datasetPath, sizeof(datasetPath) - 1, "@datasets.%s", attrLongNameEq);
      r = mongocEntityFieldReplace(entityId, datasetPath, dbAttrDatasetV, &detail);
    }
    else if (dbAttrP != NULL)  // Default attribute is being replaced
    {
      // Need the createdAt from the DB, as it must stay intact
      KjNode* creDateP = kjLookup(dbAttrP, "creDate");
      createdAt  = (creDateP != NULL)? creDateP->value.f : 0;

      KT_T(KtSR, "===================================================================================");
      KT_TREE(dbAttributeP, "API Attribute", KtSR);
      if (dbModelFromApiAttribute(dbAttributeP, NULL, NULL, NULL, NULL, true, NULL) == false)
        goto response;

      KT_TREE(dbAttributeP, "DB Attribute", KtSR);
      KT_T(KtSR, "===================================================================================");

      if (creDateP != NULL)
        dbModelAttributeCreatedAtSet(dbAttributeP, createdAt, "creDate");
      else
        KT_W("No creDate found in default instance of attribute '%s' of entity '%s'", attrLongNameEq, entityId);

      r = mongocAttributeReplace(entityId, dbAttributeP, &detail);
    }

    if (r == false)
      orionldError(OrionldInternalError, "DB Error", detail, 500);
    else
      orionldState.httpStatusCode = 204;
  }

  // DDS
  if ((ddsSupport == true) && (orionldState.ddsSample == false))  // NOT Coming in from DDS
    ddsPublishAttribute(entityId, attrName, orionldState.requestTree, false);

 response:
  // TRoE+Alterations needs the expanded attribute name for the payload body
  orionldState.requestTree->name = attrLongName;
  KT_TREE(orionldState.requestTree, "Attribute For TRoE", KtSR);

  // For Alterations
  if ((dbAttrP != NULL) || (dbAttrDatasetP != NULL))
  {
    KjNode* dbEntityCopy               = kjClone(orionldState.kjsonP, dbEntityP);
    KjNode* finalApiEntityWithSysAttrs = dbModelToApiEntity2(dbEntityCopy, true, RF_NORMALIZED, NULL, false, &orionldState.pd);

    // Need to add the sysAttrs to the new attribute
    // Might be I should clone orionldState.requestTree before adding the timestamps ...
    sysAttrs(orionldState.requestTree, createdAt, orionldState.requestTime);

    // Merge in the new attribute to finalApiEntityWithSysAttrs
    entityMergeInAttribute(finalApiEntityWithSysAttrs, orionldState.requestTree);

    KjNode* apiAttributeAsEntityP = kjObject(orionldState.kjsonP, NULL);
    kjChildAdd(apiAttributeAsEntityP, orionldState.requestTree);

    KjNode* finalApiEntity = kjClone(orionldState.kjsonP, finalApiEntityWithSysAttrs);  // Check for NULL !
    sysAttrsStrip(finalApiEntity);

    OrionldAlteration* alterationP = alteration(entityId, entityType, finalApiEntity, apiAttributeAsEntityP, dbEntityP);
    alterationP->finalApiEntityWithSysAttrsP = finalApiEntityWithSysAttrs;
  }

  return true;
}
