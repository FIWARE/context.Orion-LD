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
#include "kbase/kMacros.h"                                       // K_FT
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjObject
#include "kjson/kjStringValueLookupInArray.h"                    // kjStringValueLookupInArray
#include "kjson/kjChildCount.h"                                  // kjChildCount
}

#include "orionld/types/DistOp.h"                                // DistOp
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KTrace level
#include "orionld/common/responseFix.h"                          // responseFix
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/kjTree/kjTreeLog.h"                            // KT_TREE
#include "orionld/legacyDriver/legacyDeleteAttribute.h"          // legacyDeleteAttribute
#include "orionld/payloadCheck/pCheckUri.h"                      // pCheckUri
#include "orionld/mongoc/mongocEntityGet.h"                      // mongocEntityGet
#include "orionld/mongoc/mongocAttributeDelete.h"                // mongocAttributeDelete
#include "orionld/mongoc/mongocEntityFieldReplace.h"             // mongocEntityFieldReplace
#include "orionld/mongoc/mongocEntityFieldDelete.h"              // mongocEntityFieldDelete
#include "orionld/regMatch/regMatchForEntityGet.h"               // regMatchForEntityGet
#include "orionld/distOp/distOpListsMerge.h"                     // distOpListsMerge
#include "orionld/distOp/distOpSend.h"                           // distOpSend
#include "orionld/distOp/distOpResponses.h"                      // distOpResponses
#include "orionld/distOp/distOpFailure.h"                        // distOpFailure
#include "orionld/distOp/distOpSuccess.h"                        // distOpSuccess
#include "orionld/distOp/distOpLookupByCurlHandle.h"             // distOpLookupByCurlHandle
#include "orionld/distOp/distOpListRelease.h"                    // distOpListRelease
#include "orionld/distOp/xForwardedForCompose.h"                 // xForwardedForCompose
#include "orionld/distOp/viaCompose.h"                           // viaCompose
#include "orionld/serviceRoutines/orionldDeleteAttribute.h"      // Own interface



// -----------------------------------------------------------------------------
//
// distributedDelete -
//
DistOp* distributedDelete(KjNode* responseBody, char* entityId, char* entityTypeExpanded, char* entityTypeCompacted, char* attrNameExpanded, bool* consumedP)
{
  //
  // regMatchForEntityGet needs a StrringArray of the attribute names, as 5th parameter.
  // For DELETE Attribute, there's just a single attribute. but, the array is needed, so ...
  //
  StringArray attrV;
  char*       array[1];

  attrV.items = 1;
  attrV.array = array;
  array[0]    = attrNameExpanded;

  DistOp* exclusiveList = regMatchForEntityGet(RegModeExclusive, DoDeleteAttrs, entityId, entityTypeExpanded, &attrV, NULL);
  KT_T(KtDistOpAttrRemove, "%d attrs left", attrV.items);
  DistOp* redirectList  = (attrV.items == 1)? regMatchForEntityGet(RegModeRedirect,  DoDeleteAttrs, entityId, entityTypeExpanded, &attrV, NULL) : NULL;

  if (redirectList != NULL)
    attrV.items = 0;  // Chopped off the only attribute as it was a match to a redirect registration

  KT_T(KtDistOpAttrRemove, "%d attrs left", attrV.items);
  DistOp* inclusiveList = (attrV.items == 1)? regMatchForEntityGet(RegModeInclusive, DoDeleteAttrs, entityId, entityTypeExpanded, &attrV, NULL) : NULL;
  DistOp* distOpList;

  if (attrV.items == 0)
    *consumedP = true;

  distOpList = distOpListsMerge(exclusiveList,  redirectList);
  distOpList = distOpListsMerge(distOpList, inclusiveList);

  if (distOpList == NULL)
    return NULL;

  KT_T(KtDistOpAttrRemove, "%d attrs left", attrV.items);

  //
  // Enqueue all forwarded requests
  // Now that we've found all matching registrations we can add ourselves to the Via header
  //
  char* xff = xForwardedForCompose(orionldState.in.xForwardedFor, localIpAndPort);
  char* via = viaCompose(orionldState.in.via, brokerId);

  int forwards = 0;
  for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
  {
    // Send the forwarded request and await all responses
    if (distOpP->regP != NULL)
    {
      char dateHeader[70];
      snprintf(dateHeader, sizeof(dateHeader), "Date: %s", orionldState.requestTimeString);

      if (distOpSend(distOpP, dateHeader, xff, via, false, NULL) == 0)
      {
        ++forwards;
        distOpP->error = false;
      }
      else
      {
        KT_W("Reg %s: Forwarded request failed", distOpP->regP->regId);
        distOpP->error = true;
      }
    }
  }

  //
  // FIXME: Try to break the function in two, right here, and do local mongoc stuff in between
  //

  int stillRunning = 1;
  int loops        = 0;

  while (stillRunning != 0)
  {
    CURLMcode cm = curl_multi_perform(orionldState.curlDoMultiP, &stillRunning);
    if (cm != 0)
    {
      KT_E("Internal Error (curl_multi_perform: error %d)", cm);
      forwards = 0;
      break;
    }

    if (stillRunning != 0)
    {
      cm = curl_multi_wait(orionldState.curlDoMultiP, NULL, 0, 1000, NULL);
      if (cm != CURLM_OK)
      {
        KT_E("Internal Error (curl_multi_wait: error %d", cm);
        break;
      }
    }

    if ((++loops >= 50) && ((loops % 25) == 0))
      KT_W("curl_multi_perform doesn't seem to finish ... (%d loops)", loops);
  }

  if (loops >= 100)
    KT_W("curl_multi_perform finally finished!   (%d loops)", loops);

  // Wait for responses
  if (forwards > 0)
  {
    // Before we can call distOpResponses, all DistOp's in distOpList must have the attribute name
    for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
    {
      distOpP->attrName = orionldState.in.pathAttrExpanded;
    }

    distOpResponses(distOpList, responseBody, true);
  }

  return distOpList;
}



// ----------------------------------------------------------------------------
//
// inputCheck -
//
static bool inputCheck(const char* entityId, const char* attrName)
{
  //
  // Make sure the Entity ID is a valid URI
  //
  if (pCheckUri(entityId, "Entity ID from URL PATH", true) == false)
    return false;

  //
  // Make sure the Attribute Name is valid
  //
  if (pCheckUri(attrName, "Attribute Name from URL PATH", false) == false)
    return false;


  if ((orionldState.uriParams.datasetId != NULL) && (orionldState.uriParams.deleteAll == true))
  {
    orionldError(OrionldBadRequestData, "Invalid URL parameter combination", "Both datasetId and deleteAll provided", 400);
    return false;
  }

  return true;
}



// ----------------------------------------------------------------------------
//
// entityFromDb -
//
static KjNode* entityFromDb(const char* entityId, char** entityTypeP)
{
  const char* projectionV[] = { "attrNames", "attrs", "@datasets", NULL };

  if ((orionldState.uriParams.datasetId == NULL) && (orionldState.uriParams.deleteAll == false))
    projectionV[2] = NULL;

  KjNode* entityP = mongocEntityGet(entityId, projectionV);

  KT_TREE(entityP, "entityP", KtSR);

  if (entityP != NULL)
  {
    KjNode* _idP = kjLookup(entityP, "_id");
    KjNode* typeP = (_idP != NULL)? kjLookup(_idP, "type") : NULL;

    if (typeP != NULL)
      *entityTypeP = typeP->value.s;

    KT_T(KtSR, "entityType: '%s'", *entityTypeP);
  }

  return entityP;
}



// ----------------------------------------------------------------------------
//
// orionldDeleteAttribute -
//
// Cases:
// - No matching registrations +
//   - No datasetId => normal delete, from "attrs" and "attrNames"
//   - datasetId => delete from "@datasets"
//   - deleteAll => delete both from "@datasets" + "attrs" and "attrNames"
// - Matching registrations +
//   - No datasetId => normal delete, from "attrs" and "attrNames"
//   - datasetId => delete from "@datasets"
//   - deleteAll => delete both from "@datasets" + "attrs" and "attrNames"
//
// Also,
// If matching registrations are of type Exclusive or Redirect,
// then the attribute will be "eaten" and no further processing is required.
//
bool orionldDeleteAttribute(void)
{
  if ((experimental == false) || (orionldState.in.legacy != NULL))                      // If Legacy header - use old implementation
    return legacyDeleteAttribute();

  char* entityId     = orionldState.wildcard[0];
  char* entityType   = NULL;
  char* attrName     = orionldState.in.pathAttrExpanded;
  char* attrNameEq   = kaStrdup(&orionldState.kalloc, orionldState.in.pathAttrExpanded);

  dotForEq(attrNameEq);

  if (inputCheck(entityId, attrName) == false)
    return false;

  if ((orionldState.uriParams.datasetId != NULL) && (strcmp(orionldState.uriParams.datasetId, "@none") == 0))
    orionldState.uriParams.datasetId = NULL;

  KjNode* entityP       = NULL;
  KjNode* defaultAttrP  = NULL;
  KjNode* attrDatasetV  = NULL;
  KjNode* attrDatasetP  = NULL;
  DistOp* distOpList    = NULL;

  //
  // 1. GET the entity type => first get the entity from the DB·
  //    We need the entity type to do the forwarded requests, in case of matching registrations·
  //
  entityP = entityFromDb(entityId, &entityType);

  if (troe)
  {
    orionldState.wildcard[1]       = orionldState.in.pathAttrExpanded;
    orionldState.entityTypeForTroe = entityType;
  }

  //
  // 2. Distributed Ops - in case the operation is ONLY REMOTE
  //
  bool    consumed     = false;
  KjNode* responseBody = NULL;
  bool    distOp404    = true;

  if ((orionldState.distributed == true) && (orionldState.uriParams.local == false))
  {
    char* entityTypeCompacted = (entityType != NULL)? orionldContextItemAliasLookup(orionldState.contextP, entityType, NULL, NULL) : NULL;

    responseBody = kjObject(orionldState.kjsonP, NULL);
    distOpList   = distributedDelete(responseBody, entityId, entityType, entityTypeCompacted, orionldState.in.pathAttrExpanded, &consumed);

    if (distOpList != NULL)
    {
      for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
      {
        if (distOpP->httpResponseCode != 404)
        {
          KT_T(KtSR, "Found a non-404 DistOp response (%d)", distOpP->httpResponseCode);
          distOp404 = false;
        }
      }
    }
  }


  //
  // Has the attribute been consumed already by an Exclusive Registration?
  // In such case, we're done
  //
  if (consumed == true)
  {
    // Respond according to responses in distOpList
    KT_T(KtSR, "Consumed by registration");
    KT_TREE(responseBody, "DistOps: response", KtSR);

    int      noOf404s    = 0;
    int      noOf204s    = 0;
    int      others      = 0;
    KjNode*  bodyOther   = NULL;
    int      codeOther   = 0;
    KjNode*  body404     = NULL;

    for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
    {
      KT_T(KtSR, "DistOp Response: %d", distOpP->httpResponseCode);

      if (distOpP->httpResponseCode == 204)
        ++noOf204s;
      else if (distOpP->httpResponseCode == 404)
      {
        body404 = distOpP->responseBody;
        ++noOf404s;
      }
      else
      {
        bodyOther = distOpP->responseBody;
        codeOther = distOpP->httpResponseCode;
        ++others;
      }
    }

    KT_T(KtSR, "noOf404s: %d", noOf404s);
    KT_T(KtSR, "noOf204s: %d", noOf204s);
    KT_T(KtSR, "others:   %d", others);

    if (others != 0)  // "Weird responses. let's just give it back to the end user
    {
      orionldState.responseTree   = bodyOther;
      orionldState.httpStatusCode = codeOther;

      return false;
    }
    else if (noOf204s > 0)
    {
      orionldState.responseTree   = NULL;
      orionldState.httpStatusCode = 204;
      return true;
    }
    else if (noOf404s > 0)
    {
      orionldState.responseTree   = body404;
      orionldState.httpStatusCode = 404;

      return false;
    }

    orionldState.httpStatusCode = 204;
    return true;
  }

  //
  // 404 if no datasetId, the entity not found locally and empty distOpList
  //
  if ((orionldState.uriParams.datasetId == NULL) && (entityP == NULL) && (distOpList == NULL))
  {
    orionldError(OrionldResourceNotFound, "Entity Not Found", entityId, 404);
    return false;
  }

  //
  // 3. Lookup the default attribute inside entityP
  //
  KT_TREE(entityP, "entityP", KtSR);
  KjNode* dbAttrsP       = (entityP != NULL)? kjLookup(entityP, "attrs") : NULL;

  KT_T(KtSR, "attrNameEq: '%s'", attrNameEq);
  defaultAttrP = (dbAttrsP != NULL)? kjLookup(dbAttrsP, attrNameEq) : NULL;
  KT_T(KtSR, "dbAttrsP at %p", dbAttrsP);

  //
  // 404 if no datasetId, no default attribute found and empty distOpList (or only 404s in responseBody)
  // Also if datasetId but no instance found (later) + empty distOpList (or only 404s in responseBody)
  //
  if ((orionldState.uriParams.datasetId == NULL) && (orionldState.uriParams.deleteAll == false) && (defaultAttrP == NULL) && (distOpList == NULL))
  {
    orionldError(OrionldResourceNotFound, "Attribute Not Found", attrName, 404);
    return false;
  }

  KT_T(KtSR, "defaultAttrP: %p", defaultAttrP);
  //
  // datasetId given
  // (deleteAll also needs @datasets.attrNameEq - for 404 check, so, must take that out of the 'if')
  //
  if ((orionldState.uriParams.datasetId != NULL) || (orionldState.uriParams.deleteAll == true))
  {
    KT_T(KtSR, "defaultAttrP: %p", defaultAttrP);
    KjNode* datasetsP = (entityP != NULL)? kjLookup(entityP, "@datasets") : NULL;
    KT_T(KtSR, "defaultAttrP: %p", defaultAttrP);
    attrDatasetV = (datasetsP != NULL)? kjLookup(datasetsP, attrNameEq) : NULL;
    KT_T(KtSR, "datasetsP at %p", datasetsP);
    KT_T(KtSR, "attrDatasetV at %p", attrDatasetV);
  }

  KT_TREE(attrDatasetV, "attrDatasetV BEFORE", KtSR);

  if (attrDatasetV != NULL)
  {
    if (orionldState.uriParams.datasetId != NULL)
    {
      if (attrDatasetV->type == KjArray)
      {
        for (KjNode* dsetP = attrDatasetV->value.firstChildP; dsetP != NULL; dsetP = dsetP->next)
        {
          KjNode* dsetIdP = kjLookup(dsetP, "datasetId");
          if (dsetIdP == NULL)
          {
            KT_W("Attribute Instance without datasetId in DB (entity: '%s', attribute: '%s'", entityId, orionldState.in.pathAttrExpanded);
            continue;
          }

          if (strcmp(dsetIdP->value.s, orionldState.uriParams.datasetId) == 0)
          {
            attrDatasetP = dsetP;
            break;
          }
        }
      }
      else if (attrDatasetV->type == KjObject)
      {
        KjNode* dsetIdP = kjLookup(attrDatasetV, "datasetId");
        if (dsetIdP == NULL)
          KT_W("Attribute Instance without datasetId in DB (entity: '%s', attribute: '%s'", entityId, orionldState.in.pathAttrExpanded);
        else
        {
          if (strcmp(dsetIdP->value.s, orionldState.uriParams.datasetId) == 0)
            attrDatasetP = dsetIdP;
        }
      }
    }
  }

  KT_T(KtSR, "defaultAttrP: %p", defaultAttrP);

  if ((orionldState.uriParams.datasetId != NULL) || (orionldState.uriParams.deleteAll == true))
  {
    if (entityP == NULL)
    {
      orionldError(OrionldResourceNotFound, "Entity Not Found", entityId, 404);
      return false;
    }
    else if ((attrDatasetV == NULL) && (distOpList == NULL))
    {
      orionldError(OrionldResourceNotFound, "Attribute Not Found", attrName, 404);
      return false;
    }
  }

  KT_T(KtSR, "datasetId: '%s'", orionldState.uriParams.datasetId);
  KT_T(KtSR, "deleteAll: %s", K_FT(orionldState.uriParams.deleteAll));
  KT_T(KtSR, "attrDatasetV: %p", attrDatasetV);
  KT_T(KtSR, "attrDatasetP: %p", attrDatasetP);
  KT_T(KtSR, "defaultAttrP: %p", defaultAttrP);

  //
  // Local actual deletion of the attribute
  //
  bool deleteDefault  = false;
  bool deleteAll      = false;
  bool deleteDataset  = false;

  if (orionldState.uriParams.datasetId == NULL)
  {
    deleteDefault = true;

    if (orionldState.uriParams.deleteAll == true)
      deleteAll = true;
  }
  else if (strcmp(orionldState.uriParams.datasetId, "@none") == 0)
    deleteDefault = true;
  else
    deleteDataset = true;

  KT_T(KtSR, "deleteDefault: %s", K_FT(deleteDefault));
  KT_T(KtSR, "deleteAll:     %s", K_FT(deleteAll));
  KT_T(KtSR, "deleteDataset: %s", K_FT(deleteDataset));

  if (deleteDefault == true)
  {
    if (defaultAttrP != NULL)
    {
      KT_T(KtSR, "Deleting the default attribute '%s'", orionldState.in.pathAttrExpanded);
      int r = mongocAttributeDelete(entityId, orionldState.in.pathAttrExpanded);
      if (r == false)
      {
        orionldError(OrionldInternalError, "Database Error", "mongocAttributeDelete failed", 500);
        return false;
      }
    }
    else
    {
      if ((distOpList == NULL) && (deleteAll == false))
      {
        orionldError(OrionldResourceNotFound, "Entity Not Found", entityId, 404);
        return false;
      }
    }
  }

  char datasetPath[512];
  snprintf(datasetPath, sizeof(datasetPath) - 1, "@datasets.%s", attrNameEq);
  KT_T(KtSR, "Entity DB field to be removed/replaced: '%s' (deleteDataset: %s)", datasetPath, K_FT(deleteDataset));

  KT_T(KtSR, "attrDatasetP: %p", attrDatasetP);
  KT_T(KtSR, "defaultAttrP: %p", defaultAttrP);
  if ((attrDatasetP == NULL) && (defaultAttrP == NULL) && (distOp404 == true) && (attrDatasetV == NULL))
  {
    orionldError(OrionldResourceNotFound, "Attribute Not Found", attrName, 404);
    return false;
  }

  if (deleteDataset == true)
  {
    if (attrDatasetP != NULL)
    {
      KT_T(KtSR, "Deleting the dataset '%s' of attribute '%s'", orionldState.uriParams.datasetId, orionldState.in.pathAttrExpanded);

      bool removal = false;
      if (attrDatasetV->type == KjArray)
      {
        KT_TREE(attrDatasetV, "attrDatasetV BEFORE", KtSR);
        KT_T(KtSR, "Deleting attrDatasetP (%p) from attrDatasetV", attrDatasetP);
        kjChildRemove(attrDatasetV, attrDatasetP);
        KT_TREE(attrDatasetV, "attrDatasetV AFTER ", KtSR);
        if (attrDatasetV->value.firstChildP == NULL)  // Empty array
          removal = true;
      }
      else if (attrDatasetV->type == KjObject)
        removal = true;

      if (removal == true)
      {
        KT_T(KtSR, "ALL the dataset is being removed");
        deleteAll = true;
      }
      else  // Replace
      {
        char* detail = NULL;
        KT_T(KtSR, "Calling mongocEntityFieldReplace");
        if (mongocEntityFieldReplace(entityId, datasetPath, attrDatasetV, &detail) != true)
        {
          KT_E("mongocEntityFieldReplace failed for '%s' / '%s': %s", entityId, datasetPath, detail);
          orionldError(OrionldInternalError, "DB Error (unable to replace a dataset field in an entity)", datasetPath, 500);
          return false;
        }
      }
    }
    else
    {
      if (distOpList == NULL)
      {
        if (entityP == NULL)
          orionldError(OrionldResourceNotFound, "Entity Not Found", entityId, 404);
        else
          orionldError(OrionldResourceNotFound, "Attribute Not Found", attrName, 404);

        return false;
      }
    }
  }

  if (deleteAll == true)
  {
    char* detail = NULL;
    KT_T(KtSR, "Calling mongocEntityFieldDelete");
    if (mongocEntityFieldDelete(entityId, datasetPath, &detail) != true)
    {
      KT_E("mongocEntityFieldDelete failed for '%s' / '%s': %s", entityId, datasetPath, detail);
      orionldError(OrionldInternalError, "DB Error (unable to remove a dataset field in an entity)", datasetPath, 500);
      return false;
    }
  }

  orionldState.httpStatusCode = 204;
  return true;
}
