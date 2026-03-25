/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjString, kjFloat
}

#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/OrionldHeader.h"                         // orionldHeaderAdd, HttpResultsCount
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/pqHeader.h"                             // PGresult, PQclear, PQntuples
#include "orionld/payloadCheck/pCheckUri.h"                      // pCheckUri
#include "orionld/kjTree/kjSysAttrsRemove.h"                     // kjSysAttrsRemove
#include "orionld/context/orionldEntityCompact.h"                // orionldEntityCompact
#include "orionld/apiModel/ntosEntity.h"                         // ntosEntity
#include "orionld/apiModel/ntocEntity.h"                         // ntocEntity
#include "orionld/apiModel/ntonEntity.h"                         // ntonEntity
#include "orionld/common/pick.h"                                 // pickForEntity
#include "orionld/common/omit.h"                                 // omitForEntity
#include "orionld/common/datasetTemporalEntityFix.h"             // datasetTemporalEntityFix
#include "orionld/troe/pgTemporalEntityQuery.h"                  // pgTemporalEntityQuery
#include "orionld/troe/pgTemporalEntityBuild.h"                  // pgTemporalEntityBuild
#include "orionld/serviceRoutines/orionldGetTemporalEntity.h"    // Own Interface


extern bool troe;



// -----------------------------------------------------------------------------
//
// temporalValuesTransform -
//
// Transform the temporal entity from normalized array format to temporalValues format.
// Each attribute array of instances becomes an array of [value, observedAt] tuples.
//
// Input:  "P1": [ {"type":"Property", "value":30, "observedAt":"..."}, ... ]
// Output: "P1": { "type":"Property", "values": [[30, "..."], [20, "..."], ...] }
//
static void temporalValuesTransform(KjNode* entityP)
{
  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    // Skip id, type, scope, and system attributes
    if (attrP->type != KjArray)
      continue;

    // Build the temporalValues object
    KjNode*     valuesArray = kjArray(orionldState.kjsonP, "values");
    const char* attrType    = NULL;

    for (KjNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
    {
      if (instanceP->type != KjObject)
        continue;

      // Extract type (from first instance)
      if (attrType == NULL)
      {
        KjNode* typeP = NULL;
        for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
        {
          if (strcmp(fieldP->name, "type") == 0)
          {
            typeP = fieldP;
            break;
          }
        }
        if (typeP != NULL)
          attrType = typeP->value.s;
      }

      // Find value (or object for Relationship) and observedAt
      KjNode* valueP      = NULL;
      KjNode* observedAtP = NULL;

      for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
      {
        if (strcmp(fieldP->name, "value") == 0 || strcmp(fieldP->name, "object") == 0 || strcmp(fieldP->name, "languageMap") == 0)
          valueP = fieldP;
        else if (strcmp(fieldP->name, "observedAt") == 0)
          observedAtP = fieldP;
      }

      // Build tuple [value, observedAt]
      KjNode* tuple = kjArray(orionldState.kjsonP, NULL);

      if (valueP != NULL)
      {
        // Clone the value node without a name
        KjNode* valClone = NULL;
        switch (valueP->type)
        {
        case KjString:  valClone = kjString(orionldState.kjsonP, NULL, valueP->value.s); break;
        case KjInt:     valClone = kjInteger(orionldState.kjsonP, NULL, valueP->value.i); break;
        case KjFloat:   valClone = kjFloat(orionldState.kjsonP, NULL, valueP->value.f); break;
        case KjBoolean: valClone = kjBoolean(orionldState.kjsonP, NULL, valueP->value.b); break;
        default:        valClone = kjString(orionldState.kjsonP, NULL, ""); break;
        }
        if (valClone != NULL)
          kjChildAdd(tuple, valClone);
      }

      if (observedAtP != NULL)
        kjChildAdd(tuple, kjString(orionldState.kjsonP, NULL, observedAtP->value.s));

      kjChildAdd(valuesArray, tuple);
    }

    // Replace the array of instances with a temporalValues object
    KjNode* tvObj = kjObject(orionldState.kjsonP, attrP->name);
    if (attrType != NULL)
      kjChildAdd(tvObj, kjString(orionldState.kjsonP, "type", attrType));
    kjChildAdd(tvObj, valuesArray);

    // Replace in-place: change attrP to be the object
    attrP->type  = tvObj->type;
    attrP->value = tvObj->value;
  }
}



// ----------------------------------------------------------------------------
//
// orionldGetTemporalEntity -
//
bool orionldGetTemporalEntity(void)
{
  // Is TRoE enabled?
  if (troe == false)
  {
    orionldError(OrionldOperationNotSupported, "TRoE is not enabled - temporal operations require -troe flag", orionldState.serviceP->url, 501);
    orionldState.noLinkHeader = true;
    return false;
  }

  // Get the entity ID from the URL path
  const char* entityId = orionldState.wildcard[0];

  if (pCheckUri(entityId, "Entity ID in URL PATH", true) == false)
    return false;

  // Validate temporal query parameters
  const char* timerel      = orionldState.uriParams.timerel;
  const char* timeAt       = orionldState.uriParams.timeAt;
  const char* endTimeAt    = orionldState.uriParams.endTimeAt;
  const char* timeproperty = orionldState.uriParams.timeproperty;
  int         lastN        = orionldState.uriParams.lastN;

  // Per ETSI GS CIM 009 clause 6.19.3.1, timerel and timeAt are optional (cardinality 0..1)
  // If both are omitted, all attribute instances are returned without time filtering
  if (timerel != NULL && timeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter 'timeAt' when 'timerel' is present", "timeAt", 400);
    return false;
  }

  if (timerel == NULL && timeAt != NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter 'timerel' when 'timeAt' is present", "timerel", 400);
    return false;
  }

  // Validate timerel value (if provided)
  if (timerel != NULL && strcmp(timerel, "before") != 0 && strcmp(timerel, "after") != 0 && strcmp(timerel, "between") != 0)
  {
    orionldError(OrionldBadRequestData, "Invalid value for URI parameter 'timerel'", timerel, 400);
    return false;
  }

  // For "between", endTimeAt is required
  if (timerel != NULL && strcmp(timerel, "between") == 0 && endTimeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter 'endTimeAt' for timerel=between", "endTimeAt", 400);
    return false;
  }

  // Validate lastN
  if (lastN < 0)
  {
    orionldError(OrionldBadRequestData, "Invalid value for URI parameter 'lastN'", "must be a positive integer", 400);
    return false;
  }

  // pick and omit are mutually exclusive
  if (orionldState.uriParams.pick != NULL && orionldState.uriParams.omit != NULL)
  {
    orionldError(OrionldBadRequestData, "Incompatible URI parameters", "pick and omit cannot be used together", 400);
    return false;
  }

  // Execute the temporal queries against TRoE
  PGresult* entityRes  = NULL;
  PGresult* attrRes    = NULL;
  PGresult* subAttrRes = NULL;

  if (pgTemporalEntityQuery(entityId, timerel, timeAt, endTimeAt, timeproperty,
                             &orionldState.in.attrList, lastN,
                             &entityRes, &attrRes, &subAttrRes) == false)
  {
    orionldError(OrionldInternalError, "Database Error", "temporal query against TRoE failed", 500);
    return false;
  }

  // Check if entity was found
  if (PQntuples(entityRes) == 0)
  {
    PQclear(entityRes);
    if (attrRes != NULL)    PQclear(attrRes);
    if (subAttrRes != NULL) PQclear(subAttrRes);

    orionldError(OrionldResourceNotFound, "Entity Not Found", entityId, 404);
    return false;
  }

  // Add NGSILD-Results-Count header if count=true (total attribute instance rows)
  if (orionldState.uriParams.count == true && attrRes != NULL)
    orionldHeaderAdd(&orionldState.out.headers, HttpResultsCount, NULL, PQntuples(attrRes));

  // Build the NGSI-LD entity from the query results
  KjNode* apiEntityP = pgTemporalEntityBuild(entityRes, attrRes, subAttrRes);

  // Clean up PGresult handles
  PQclear(entityRes);
  if (attrRes != NULL)    PQclear(attrRes);
  if (subAttrRes != NULL) PQclear(subAttrRes);

  if (apiEntityP == NULL)
  {
    orionldError(OrionldInternalError, "Internal Error", "unable to build temporal entity response", 500);
    return false;
  }

  // Compact attribute names using the request's @context
  orionldEntityCompact(apiEntityP, orionldState.contextP);

  // Apply datasetId filter (before format transformations, as it removes instances)
  if (orionldState.uriParams.datasetId != NULL)
    datasetTemporalEntityFix(apiEntityP);

  // Apply output format transformation
  bool   sysAttrs = orionldState.uriParamOptions.sysAttrs;
  char*  lang     = orionldState.uriParams.lang;

  if      (orionldState.out.format == RF_SIMPLIFIED) ntosEntity(apiEntityP, lang);
  else if (orionldState.out.format == RF_CONCISE)    ntocEntity(apiEntityP, lang, sysAttrs);
  else                                               ntonEntity(apiEntityP, lang, sysAttrs);

  // Apply temporalValues transformation if requested
  if (orionldState.uriParams.format != NULL && strcmp(orionldState.uriParams.format, "temporalValues") == 0)
    temporalValuesTransform(apiEntityP);

  if (sysAttrs == false)
    kjSysAttrsRemove(apiEntityP, 2);

  // Apply pick/omit post-processing filters (after compaction and sysAttrs removal)
  if (orionldState.in.pickList.items > 0)
    pickForEntity(apiEntityP);

  if (orionldState.in.omitList.items > 0)
    omitForEntity(apiEntityP);

  orionldState.responseTree   = apiEntityP;
  orionldState.httpStatusCode = 200;

  return true;
}
