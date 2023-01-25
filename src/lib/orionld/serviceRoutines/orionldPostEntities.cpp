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
#include <unistd.h>                                              // NULL, gethostname
#include <curl/curl.h>                                           // curl

extern "C"
{
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjString, kjObject, ...
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "rest/httpHeaderAdd.h"                                  // httpHeaderLocationAdd

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/performance.h"                          // PERFORMANCE
#include "orionld/common/curlToBrokerStrerror.h"                 // curlToBrokerStrerror
#include "orionld/legacyDriver/legacyPostEntities.h"             // legacyPostEntities
#include "orionld/payloadCheck/PCHECK.h"                         // PCHECK_*
#include "orionld/payloadCheck/pCheckEntityId.h"                 // pCheckEntityId
#include "orionld/payloadCheck/pCheckEntityType.h"               // pCheckEntityType
#include "orionld/payloadCheck/pCheckEntity.h"                   // pCheckEntity
#include "orionld/payloadCheck/pCheckUri.h"                      // pCheckUri
#include "orionld/dbModel/dbModelFromApiEntity.h"                // dbModelFromApiEntity
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup
#include "orionld/mongoc/mongocEntityInsert.h"                   // mongocEntityInsert
#include "orionld/forwarding/DistOp.h"                           // DistOp
#include "orionld/forwarding/distOpSend.h"                       // distOpSend
#include "orionld/forwarding/distOpListRelease.h"                // distOpListRelease
#include "orionld/forwarding/distOpSuccess.h"                    // distOpSuccess
#include "orionld/forwarding/distOpFailure.h"                    // distOpFailure
#include "orionld/forwarding/distOpLookupByCurlHandle.h"         // distOpLookupByCurlHandle
#include "orionld/forwarding/regMatchForEntityCreation.h"        // regMatchForEntityCreation
#include "orionld/forwarding/distOpListsMerge.h"                 // distOpListsMerge
#include "orionld/forwarding/xForwardedForCompose.h"             // xForwardedForCompose
#include "orionld/serviceRoutines/orionldPostEntities.h"         // Own interface



// -----------------------------------------------------------------------------
//
// purgeRedirectedAttributes -
//
// After the redirected registrations are dealt with, we need to chop attributes off the body.
// And after that continue with Inclusive registrations (and local DB)
//
void purgeRedirectedAttributes(DistOp* redirectList, KjNode* body)
{
  for (DistOp* distOpP = redirectList; distOpP != NULL; distOpP = distOpP->next)
  {
    for (KjNode* attrP = distOpP->body->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      KjNode* bodyAttrP = kjLookup(body, attrP->name);

      if (bodyAttrP != NULL)
        kjChildRemove(body, bodyAttrP);
    }
  }
}



// ----------------------------------------------------------------------------
//
// orionldPostEntities -
//
bool orionldPostEntities(void)
{
  if ((experimental == false) || (orionldState.in.legacy != NULL))                      // If Legacy header - use old implementation
    return legacyPostEntities();

  PCHECK_OBJECT(orionldState.requestTree, 0, NULL, "To create an Entity, a JSON OBJECT defining the entity must be provided", 400);

  char*  entityId;
  char*  entityType;

  if (pCheckEntityId(orionldState.payloadIdNode,     true, &entityId)   == false)   return false;
  if (pCheckEntityType(orionldState.payloadTypeNode, true, &entityType) == false)   return false;


  //
  // Check and fix the incoming payload (entity)
  //
  if (pCheckEntity(orionldState.requestTree, false, NULL) == false)
    return false;

  int      forwards      = 0;
  KjNode*  responseBody  = NULL;
  DistOp*  distOpList    = NULL;

  if (orionldState.distributed)
  {
    KjNode* entityIdNodeP = kjString(orionldState.kjsonP, "entityId", entityId);  // Only used if 207 response

    responseBody = kjObject(orionldState.kjsonP, NULL);                           // Only used if 207 response
    kjChildAdd(responseBody, entityIdNodeP);

    char dateHeader[70];
    snprintf(dateHeader, sizeof(dateHeader), "Date: %s", orionldState.requestTimeString);

    //
    // NOTE:  Need to be very careful here that we treat first all EXCLUSIVE registrations, as they "chop off" attributes to be forwarded
    //        The other two (redirect, inclusive) don't, BUT only the remaining attributes AFTER the Exclusive regs have chopped off
    //        attributes shall be forwarded for redirect+inclusive registrations.
    //        Auxiliary registrations aren't allowed to receive create/update forwardes messages
    //
    // The fix is as follows:
    //   DistOp* exclusiveList = regMatchForEntityCreation(RegModeExclusive, DoCreateEntity, entityId, entityType, orionldState.requestTree); (chopping attrs off)
    //   DistOp* redirectList  = regMatchForEntityCreation(RegModeRedirect,  DoCreateEntity, entityId, entityType, orionldState.requestTree);
    //
    //   purgeRedirectedAttributes(redirectList, orionldState.requestTree);
    //
    //   DistOp* inclusiveList = regMatchForEntityCreation(RegModeInclusive, DoCreateEntity, entityId, entityType, orionldState.requestTree);
    //
    //   distOpList = distOpListsMerge(exclusiveList, redirectList);
    //   distOpList = distOpListsMerge(distOpList, inclusiveList);
    //
    //   - Loop over distOpList
    //
    // Example:
    // - The Entity to be created (and distributed) has "id" "urn:E1", "type": "T" and 10 Properties P1-P10
    // - Exclusive registration R1 of urn:E1/T+P1
    // - Exclusive registration R2 of urn:E1/T+P2
    // - Redirect  registration R3 of urn:E1/T+P3+P4+P5 (or just T+P3+P4+P5)
    // - Redirect  registration R4 of urn:E1/T+P4+P5+P6
    // - Inclusive registration R5 of urn:E1/T+P7+P8+P9+P10 (not possible to include P1-P6)
    //
    // The entity will be distributed like this:
    // - P1 on R1::endpoint
    // - P2 on R2::endpoint
    // - P3-P5 on R3::endpoint
    // - P4-P6 on R4::endpoint
    // - P7-P10 on R5::endpoint
    // - P7-P10 on local broker
    //
    DistOp* exclusiveList = regMatchForEntityCreation(RegModeExclusive, DoCreateEntity, entityId, entityType, orionldState.requestTree);  // chopping attrs off orionldState.requestTree
    DistOp* redirectList  = regMatchForEntityCreation(RegModeRedirect,  DoCreateEntity, entityId, entityType, orionldState.requestTree);

    if (redirectList != NULL)
      purgeRedirectedAttributes(redirectList, orionldState.requestTree);  // chopping attrs off orionldState.requestTree

    DistOp* inclusiveList = regMatchForEntityCreation(RegModeInclusive, DoCreateEntity, entityId, entityType, orionldState.requestTree);
    distOpList = distOpListsMerge(exclusiveList, redirectList);
    distOpList = distOpListsMerge(distOpList, inclusiveList);

    // Enqueue all forwarded requests
    if (distOpList != NULL)
    {
      // Now that we've found all matching registrations we can add ourselves to the X-forwarded-For header
      char* xff = xForwardedForCompose(orionldState.in.xForwardedFor, localIpAndPort);

      for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
      {
        // Send the forwarded request and await all responses
        if (distOpP->regP != NULL)
        {
          if (distOpSend(distOpP, dateHeader, xff) == 0)
          {
            ++forwards;
            distOpP->error = false;
          }
          else
            distOpP->error = true;
        }
      }

      int stillRunning = 1;
      int loops        = 0;

      while (stillRunning != 0)
      {
        CURLMcode cm = curl_multi_perform(orionldState.curlDoMultiP, &stillRunning);
        if (cm != 0)
        {
          LM_E(("Internal Error (curl_multi_perform: error %d)", cm));
          forwards = 0;
          break;
        }

        if (stillRunning != 0)
        {
          cm = curl_multi_wait(orionldState.curlDoMultiP, NULL, 0, 1000, NULL);
          if (cm != CURLM_OK)
          {
            LM_E(("Internal Error (curl_multi_wait: error %d", cm));
            break;
          }
        }

        if ((++loops >= 50) && ((loops % 25) == 0))
          LM_W(("curl_multi_perform doesn't seem to finish ... (%d loops)", loops));
      }

      if (loops >= 100)
        LM_W(("curl_multi_perform finally finished!   (%d loops)", loops));

      // Anything left for a local entity?
      if (orionldState.requestTree->value.firstChildP != NULL)
      {
        KjNode* entityIdP   = kjString(orionldState.kjsonP,  "id",   entityId);
        KjNode* entityTypeP = kjString(orionldState.kjsonP,  "type", entityType);

        kjChildAdd(orionldState.requestTree, entityIdP);
        kjChildAdd(orionldState.requestTree, entityTypeP);
      }
      else
        orionldState.requestTree = NULL;  // Meaning: nothing left for local DB
    }
  }


  //
  // If the entity already exists, a "409 Conflict" is returned, either complete or as part of a 207
  //
  if (orionldState.requestTree != NULL)
  {
    if (mongocEntityLookup(entityId, NULL, NULL, NULL) != NULL)
    {
      if (distOpList == NULL)  // Purely local request
      {
        orionldError(OrionldAlreadyExists, "Entity already exists", entityId, 409);
        return false;
      }
      else
      {
        distOpFailure(responseBody, NULL, OrionldAlreadyExists, "Entity already exists", entityId, 404);
        goto awaitDoResponses;
      }
    }

    //
    // NOTE
    //   payloadParseAndExtractSpecialFields() from orionldMhdConnectionTreat() decouples the entity id and type
    //   from the payload body, so, the entity type is not expanded by pCheckEntity()
    //   The expansion is instead done by payloadTypeNodeFix, called by orionldMhdConnectionTreat
    //

    // dbModelFromApiEntity destroys the tree, need to make a copy for notifications
    KjNode* apiEntityP = kjClone(orionldState.kjsonP, orionldState.requestTree);

    // Entity id and type was removed - they need to go back
    orionldState.payloadIdNode->next   = orionldState.payloadTypeNode;
    orionldState.payloadTypeNode->next = apiEntityP->value.firstChildP;
    apiEntityP->value.firstChildP      = orionldState.payloadIdNode;


    //
    // The current shape of the incoming tree is now fit for TRoE, while it still needs to be adjusted for mongo,
    // If TRoE is enabled we clone it here, for later use in TRoE processing
    //
    // The same tree (read-only) might also be necessary for a 207 response in a distributed operation.
    // So, if anythis has been forwarded, the clone is made regardless whether TRoE is enabled.
    //
    KjNode* cloneForTroeP = NULL;
    if ((troe == true) || (distOpList != NULL))
      cloneForTroeP = kjClone(orionldState.kjsonP, apiEntityP);  // apiEntityP contains entity ID and TYPE

    if (dbModelFromApiEntity(orionldState.requestTree, NULL, true, orionldState.payloadIdNode->value.s, orionldState.payloadTypeNode->value.s) == false)
    {
      //
      // Not calling orionldError as a better error message is overwritten if I do.
      // Once we have "Error Stacking", orionldError should be called.
      //
      // orionldError(OrionldInternalError, "Internal Error", "Unable to convert API Entity into DB Model Entity", 500);

      if (distOpList == NULL)  // Purely local request
        return false;
      else
      {
        distOpFailure(responseBody, NULL, OrionldInternalError, "Internal Error", "dbModelFromApiEntity failed", 500);
        goto awaitDoResponses;
      }
    }

    KjNode* dbEntityP = orionldState.requestTree;  // More adequate to talk about DB-Entity from here on

    // datasets?
    if (orionldState.datasets != NULL)
      kjChildAdd(dbEntityP, orionldState.datasets);

    // Ready to send it to the database
    if (mongocEntityInsert(dbEntityP, entityId) == false)
    {
      orionldError(OrionldInternalError, "Database Error", "mongocEntityInsert failed", 500);
      if (distOpList == NULL)  // Purely local request
        return false;
      else
      {
        distOpFailure(responseBody, NULL, OrionldInternalError, "Database Error", "mongocEntityInsert failed", 500);
        goto awaitDoResponses;
      }
    }
    else if (distOpList != NULL)  // NOT Purely local request
    {
      //
      // Need to call distOpSuccess here. Only, I don't have a DistOp object for local ...
      // Only the "DistOp::body" is used in distOpSuccess so I can fix it:
      //
      DistOp local;
      local.body = cloneForTroeP;
      distOpSuccess(responseBody, &local);
    }

    //
    // Prepare for notifications
    //
    orionldState.alterations = (OrionldAlteration*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAlteration));
    orionldState.alterations->entityId          = entityId;
    orionldState.alterations->entityType        = entityType;
    orionldState.alterations->inEntityP         = apiEntityP;
    orionldState.alterations->dbEntityP         = NULL;
    orionldState.alterations->finalApiEntityP   = apiEntityP;  // entity id, createdAt, modifiedAt ...
    orionldState.alterations->alteredAttributes = 0;
    orionldState.alterations->alteredAttributeV = NULL;
    orionldState.alterations->next              = NULL;

    // All good
    orionldState.httpStatusCode = 201;
    httpHeaderLocationAdd("/ngsi-ld/v1/entities/", entityId, orionldState.tenantP->tenant);

    if (cloneForTroeP != NULL)
      orionldState.requestTree = cloneForTroeP;

    if (distOpList == NULL)  // Purely local request
      return true;
  }

 awaitDoResponses:
  if (forwards > 0)
  {
    CURLMsg* msgP;
    int      msgsLeft;

    while ((msgP = curl_multi_info_read(orionldState.curlDoMultiP, &msgsLeft)) != NULL)
    {
      if (msgP->msg != CURLMSG_DONE)
        continue;

      DistOp* distOpP = distOpLookupByCurlHandle(distOpList, msgP->easy_handle);

      if (msgP->data.result == CURLE_OK)
        distOpSuccess(responseBody, distOpP);
      else
      {
        int          statusCode = 500;
        const char*  detail     = curlToBrokerStrerror(msgP->easy_handle, msgP->data.result, &statusCode);

        LM_E(("CURL Error %d: %s", msgP->data.result, curl_easy_strerror(msgP->data.result)));
        distOpFailure(responseBody, distOpP, OrionldInternalError, "Error during Distributed Operation", detail, statusCode);
      }
    }
  }

  if ((responseBody != NULL) && (kjLookup(responseBody, "failure") != NULL))
  {
    orionldState.httpStatusCode = 207;
    orionldState.responseTree   = responseBody;
  }
  else
  {
    // All good
    if (orionldState.httpStatusCode != 201)  // Cause, this might be done already - inside local processing of entity
    {
      orionldState.httpStatusCode = 201;
      httpHeaderLocationAdd("/ngsi-ld/v1/entities/", entityId, orionldState.tenantP->tenant);
    }
  }

  if (orionldState.curlDoMultiP != NULL)
    distOpListRelease(distOpList);

  return true;
}
