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
* Author: Carsten Frey
*/
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjObject, kjChildAdd
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjChildCount.h"                                  // kjChildCount
#include "kalloc/kaAlloc.h"                                      // kaAlloc
}

#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/OrionldHeader.h"                         // orionldHeaderAdd, HttpResultsCount
#include "orionld/types/StringArray.h"                           // StringArray
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/pqHeader.h"                             // PGresult, PQclear, PQntuples, PQgetvalue
#include "orionld/kjTree/kjSysAttrsRemove.h"                     // kjSysAttrsRemove
#include "orionld/context/orionldEntityCompact.h"                // orionldEntityCompact
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/apiModel/ntosEntity.h"                         // ntosEntity
#include "orionld/apiModel/ntocEntity.h"                         // ntocEntity
#include "orionld/apiModel/ntonEntity.h"                         // ntonEntity
#include "orionld/common/pick.h"                                 // pickForEntity
#include "orionld/common/omit.h"                                 // omitForEntity
#include "orionld/common/datasetTemporalEntityFix.h"             // datasetTemporalEntityFix
#include "orionld/common/temporalValuesTransform.h"              // temporalValuesTransform
#include "orionld/troe/qTreeToSql.h"                             // troeQStringToSql
#include "orionld/troe/pgTemporalEntitiesQuery.h"                // pgTemporalEntitiesQuery
#include "orionld/troe/pgTemporalEntityQuery.h"                  // pgTemporalEntityQuery
#include "orionld/troe/pgTemporalEntityBuild.h"                  // pgTemporalEntityBuild
#include "orionld/serviceRoutines/orionldPostTemporalQuery.h"    // Own Interface


extern bool troe;



// -----------------------------------------------------------------------------
//
// pCheckTemporalQ - parse and validate the temporalQ object
//
// Returns false on error (orionldError already called).
// On success, the output parameters are set.
//
static bool pCheckTemporalQ
(
  KjNode*      temporalQP,
  const char** timerelP,
  const char** timeAtP,
  const char** endTimeAtP,
  const char** timepropertyP
)
{
  const char* timerel      = NULL;
  const char* timeAt       = NULL;
  const char* endTimeAt    = NULL;
  const char* timeproperty = NULL;

  for (KjNode* nodeP = temporalQP->value.firstChildP; nodeP != NULL; nodeP = nodeP->next)
  {
    if (strcmp(nodeP->name, "timerel") == 0)
    {
      if (nodeP->type != KjString)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "temporalQ::timerel must be a String", 400);
        return false;
      }
      timerel = nodeP->value.s;
    }
    else if (strcmp(nodeP->name, "timeAt") == 0)
    {
      if (nodeP->type != KjString)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "temporalQ::timeAt must be a String", 400);
        return false;
      }
      timeAt = nodeP->value.s;
    }
    else if (strcmp(nodeP->name, "endTimeAt") == 0)
    {
      if (nodeP->type != KjString)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "temporalQ::endTimeAt must be a String", 400);
        return false;
      }
      endTimeAt = nodeP->value.s;
    }
    else if (strcmp(nodeP->name, "timeproperty") == 0)
    {
      if (nodeP->type != KjString)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "temporalQ::timeproperty must be a String", 400);
        return false;
      }
      timeproperty = nodeP->value.s;
    }
    else
    {
      orionldError(OrionldBadRequestData, "Unknown field in temporalQ", nodeP->name, 400);
      return false;
    }
  }

  if (timerel == NULL)
  {
    orionldError(OrionldBadRequestData, "Mandatory field missing", "temporalQ::timerel", 400);
    return false;
  }

  if (timeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Mandatory field missing", "temporalQ::timeAt", 400);
    return false;
  }

  if (strcmp(timerel, "before") != 0 && strcmp(timerel, "after") != 0 && strcmp(timerel, "between") != 0)
  {
    orionldError(OrionldBadRequestData, "Invalid value for temporalQ::timerel", timerel, 400);
    return false;
  }

  if (strcmp(timerel, "between") == 0 && endTimeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Mandatory field missing for timerel=between", "temporalQ::endTimeAt", 400);
    return false;
  }

  *timerelP      = timerel;
  *timeAtP       = timeAt;
  *endTimeAtP    = endTimeAt;
  *timepropertyP = timeproperty;

  return true;
}



// -----------------------------------------------------------------------------
//
// entitiesExtract - extract type/id/idPattern from entities array into StringArrays
//
// The entities array contains EntitySelector objects: { type, id, idPattern }
// We flatten these into typeList and idList, and take the first idPattern found.
//
static bool entitiesExtract
(
  KjNode*      entitiesP,
  StringArray* typeList,
  StringArray* idList,
  const char** idPatternP
)
{
  int entityCount = kjChildCount(entitiesP);

  // Allocate max-size arrays (one type/id per entity selector)
  typeList->array = (char**) kaAlloc(&orionldState.kalloc, sizeof(char*) * entityCount);
  typeList->items = 0;
  idList->array   = (char**) kaAlloc(&orionldState.kalloc, sizeof(char*) * entityCount);
  idList->items   = 0;
  *idPatternP     = NULL;

  for (KjNode* entityP = entitiesP->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    if (entityP->type != KjObject)
    {
      orionldError(OrionldBadRequestData, "Invalid JSON type", "entities array item must be a JSON Object", 400);
      return false;
    }

    KjNode* typeP      = NULL;
    KjNode* idP        = NULL;
    KjNode* idPatternP2 = NULL;

    for (KjNode* nodeP = entityP->value.firstChildP; nodeP != NULL; nodeP = nodeP->next)
    {
      if (strcmp(nodeP->name, "type") == 0)
      {
        if (nodeP->type != KjString)
        {
          orionldError(OrionldBadRequestData, "Invalid JSON type", "entities::type must be a String", 400);
          return false;
        }
        typeP = nodeP;
      }
      else if (strcmp(nodeP->name, "id") == 0)
      {
        if (nodeP->type != KjString)
        {
          orionldError(OrionldBadRequestData, "Invalid JSON type", "entities::id must be a String", 400);
          return false;
        }
        idP = nodeP;
      }
      else if (strcmp(nodeP->name, "idPattern") == 0)
      {
        if (nodeP->type != KjString)
        {
          orionldError(OrionldBadRequestData, "Invalid JSON type", "entities::idPattern must be a String", 400);
          return false;
        }
        idPatternP2 = nodeP;
      }
      else
      {
        orionldError(OrionldBadRequestData, "Unknown field in entities item", nodeP->name, 400);
        return false;
      }
    }

    if (typeP == NULL)
    {
      orionldError(OrionldBadRequestData, "Mandatory field missing", "entities::type", 400);
      return false;
    }

    // Expand the type
    typeP->value.s = orionldContextItemExpand(orionldState.contextP, typeP->value.s, true, NULL);
    typeList->array[typeList->items++] = typeP->value.s;

    // id takes precedence over idPattern (per spec)
    if (idP != NULL)
      idList->array[idList->items++] = idP->value.s;
    else if (idPatternP2 != NULL && *idPatternP == NULL)
      *idPatternP = idPatternP2->value.s;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// attrsExtract - extract and expand attribute names from attrs array into StringArray
//
static bool attrsExtract(KjNode* attrsP, StringArray* attrList)
{
  int attrCount = kjChildCount(attrsP);

  attrList->array = (char**) kaAlloc(&orionldState.kalloc, sizeof(char*) * attrCount);
  attrList->items = 0;

  for (KjNode* attrP = attrsP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type != KjString)
    {
      orionldError(OrionldBadRequestData, "Invalid JSON type", "attrs array item must be a String", 400);
      return false;
    }

    attrP->value.s = orionldAttributeExpand(orionldState.contextP, attrP->value.s, true, NULL);
    attrList->array[attrList->items++] = attrP->value.s;
  }

  return true;
}



// ----------------------------------------------------------------------------
//
// orionldPostTemporalQuery -
//
bool orionldPostTemporalQuery(void)
{
  if (troe == false)
  {
    orionldError(OrionldOperationNotSupported, "TRoE is not enabled - temporal operations require -troe flag", orionldState.serviceP->url, 501);
    orionldState.noLinkHeader = true;
    return false;
  }

  KjNode* requestTree = orionldState.requestTree;

  if (requestTree == NULL || requestTree->type != KjObject)
  {
    orionldError(OrionldBadRequestData, "Invalid request body", "must be a JSON Object", 400);
    return false;
  }

  //
  // Parse top-level fields: type, entities, attrs, temporalQ, q
  //
  KjNode*     typeNodeP    = NULL;
  KjNode*     entitiesP    = NULL;
  KjNode*     attrsP       = NULL;
  KjNode*     temporalQP   = NULL;
  KjNode*     qNodeP       = NULL;

  for (KjNode* nodeP = requestTree->value.firstChildP; nodeP != NULL; nodeP = nodeP->next)
  {
    if (strcmp(nodeP->name, "type") == 0)
    {
      if (nodeP->type != KjString)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "type must be a String", 400);
        return false;
      }
      typeNodeP = nodeP;
    }
    else if (strcmp(nodeP->name, "entities") == 0)
    {
      if (nodeP->type != KjArray)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "entities must be a JSON Array", 400);
        return false;
      }
      if (nodeP->value.firstChildP == NULL)
      {
        orionldError(OrionldBadRequestData, "Empty Array", "entities", 400);
        return false;
      }
      entitiesP = nodeP;
    }
    else if (strcmp(nodeP->name, "attrs") == 0)
    {
      if (nodeP->type != KjArray)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "attrs must be a JSON Array", 400);
        return false;
      }
      if (nodeP->value.firstChildP == NULL)
      {
        orionldError(OrionldBadRequestData, "Empty Array", "attrs", 400);
        return false;
      }
      attrsP = nodeP;
    }
    else if (strcmp(nodeP->name, "temporalQ") == 0)
    {
      if (nodeP->type != KjObject)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "temporalQ must be a JSON Object", 400);
        return false;
      }
      if (nodeP->value.firstChildP == NULL)
      {
        orionldError(OrionldBadRequestData, "Empty Object", "temporalQ", 400);
        return false;
      }
      temporalQP = nodeP;
    }
    else if (strcmp(nodeP->name, "q") == 0)
    {
      if (nodeP->type != KjString)
      {
        orionldError(OrionldBadRequestData, "Invalid JSON type", "q must be a String", 400);
        return false;
      }
      if (nodeP->value.s[0] == 0)
      {
        orionldError(OrionldBadRequestData, "Empty String", "q", 400);
        return false;
      }
      qNodeP = nodeP;
    }
    else if (strcmp(nodeP->name, "geoQ") == 0 || strcmp(nodeP->name, "scopeQ") == 0)
    {
      orionldError(OrionldOperationNotSupported, "Not Implemented", nodeP->name, 501);
      return false;
    }
    else if (strcmp(nodeP->name, "csf") == 0)
    {
      orionldError(OrionldBadRequestData, "Not Supported", "csf", 400);
      return false;
    }
    else if (strcmp(nodeP->name, "lang") == 0 || strcmp(nodeP->name, "local") == 0)
    {
      // Accepted but not actively used (lang is taken from body if present)
    }
    else
    {
      orionldError(OrionldBadRequestData, "Unknown field in Query body", nodeP->name, 400);
      return false;
    }
  }

  // type is mandatory and must be "Query"
  if (typeNodeP == NULL)
  {
    orionldError(OrionldBadRequestData, "Mandatory field missing", "type", 400);
    return false;
  }
  if (strcmp(typeNodeP->value.s, "Query") != 0)
  {
    orionldError(OrionldBadRequestData, "Invalid value for 'type'", "must be 'Query'", 400);
    return false;
  }

  // temporalQ is mandatory for temporal queries
  if (temporalQP == NULL)
  {
    orionldError(OrionldBadRequestData, "Mandatory field missing", "temporalQ", 400);
    return false;
  }

  // entities or attrs must be present
  if (entitiesP == NULL && attrsP == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required field", "entities or attrs must be provided", 400);
    return false;
  }

  // Parse temporalQ
  const char* timerel      = NULL;
  const char* timeAt       = NULL;
  const char* endTimeAt    = NULL;
  const char* timeproperty = NULL;

  if (pCheckTemporalQ(temporalQP, &timerel, &timeAt, &endTimeAt, &timeproperty) == false)
    return false;

  // Extract entity selectors into type/id/idPattern lists
  StringArray typeList    = { 0, NULL };
  StringArray idList      = { 0, NULL };
  const char* idPattern   = NULL;

  if (entitiesP != NULL)
  {
    if (entitiesExtract(entitiesP, &typeList, &idList, &idPattern) == false)
      return false;
  }

  // Extract and expand attrs
  StringArray attrList = { 0, NULL };
  if (attrsP != NULL)
  {
    if (attrsExtract(attrsP, &attrList) == false)
      return false;
  }

  // URI parameters for pagination and post-processing
  int  limit  = orionldState.uriParams.limit;
  int  offset = orionldState.uriParams.offset;
  int  lastN  = orionldState.uriParams.lastN;

  // pick and omit are mutually exclusive
  if (orionldState.uriParams.pick != NULL && orionldState.uriParams.omit != NULL)
  {
    orionldError(OrionldBadRequestData, "Incompatible URI parameters", "pick and omit cannot be used together", 400);
    return false;
  }

  if (lastN < 0)
  {
    orionldError(OrionldBadRequestData, "Invalid value for URI parameter 'lastN'", "must be a positive integer", 400);
    return false;
  }

  //
  // Parse q-parameter if present (from POST body)
  //
  const char* qFilter = NULL;
  if (qNodeP != NULL)
  {
    qFilter = troeQStringToSql(qNodeP->value.s);
    if (qFilter == NULL)
      return false;  // troeQStringToSql already set the error
  }

  //
  // Step 1: Discover matching entities with pagination
  //
  long long  count     = 0;
  long long* countP    = orionldState.uriParams.count ? &count : NULL;
  PGresult*  entityRes = NULL;

  if (pgTemporalEntitiesQuery(&typeList, &idList, idPattern,
                              timerel, timeAt, endTimeAt, qFilter,
                              limit, offset, countP, &entityRes) == false)
  {
    orionldError(OrionldInternalError, "Database Error", "temporal entities query failed", 500);
    return false;
  }

  // Add count header if requested
  if (orionldState.uriParams.count)
    orionldHeaderAdd(&orionldState.out.headers, HttpResultsCount, NULL, count);

  int entityRows = PQntuples(entityRes);

  // Empty result
  if (entityRows == 0)
  {
    PQclear(entityRes);
    orionldState.responseTree   = kjArray(orionldState.kjsonP, NULL);
    orionldState.httpStatusCode = 200;
    return true;
  }

  //
  // Step 2: For each entity, query its temporal representation and build response
  //
  KjNode* resultArray = kjArray(orionldState.kjsonP, NULL);
  bool    sysAttrs    = orionldState.uriParamOptions.sysAttrs;
  char*   lang        = orionldState.uriParams.lang;

  for (int row = 0; row < entityRows; row++)
  {
    const char* entityId = PQgetvalue(entityRes, row, 0);

    // Query this entity's temporal data
    PGresult* eRes  = NULL;
    PGresult* aRes  = NULL;
    PGresult* saRes = NULL;

    if (pgTemporalEntityQuery(entityId, timerel, timeAt, endTimeAt, timeproperty,
                              &attrList, lastN,
                              &eRes, &aRes, &saRes) == false)
    {
      KT_E("pgTemporalEntityQuery failed for entity '%s'", entityId);
      continue;
    }

    if (PQntuples(eRes) == 0)
    {
      PQclear(eRes);
      if (aRes  != NULL) PQclear(aRes);
      if (saRes != NULL) PQclear(saRes);
      continue;
    }

    // Build the NGSI-LD temporal entity
    KjNode* apiEntityP = pgTemporalEntityBuild(eRes, aRes, saRes);

    PQclear(eRes);
    if (aRes  != NULL) PQclear(aRes);
    if (saRes != NULL) PQclear(saRes);

    if (apiEntityP == NULL)
      continue;

    // Post-processing (same as GET /temporal/entities)
    orionldEntityCompact(apiEntityP, orionldState.contextP);

    if (orionldState.uriParams.datasetId != NULL)
      datasetTemporalEntityFix(apiEntityP);

    if      (orionldState.out.format == RF_SIMPLIFIED) ntosEntity(apiEntityP, lang);
    else if (orionldState.out.format == RF_CONCISE)    ntocEntity(apiEntityP, lang, sysAttrs);
    else                                               ntonEntity(apiEntityP, lang, sysAttrs);

    if (orionldState.uriParams.format != NULL && strcmp(orionldState.uriParams.format, "temporalValues") == 0)
      temporalValuesTransform(apiEntityP);

    if (sysAttrs == false)
      kjSysAttrsRemove(apiEntityP, 2);

    if (orionldState.in.pickList.items > 0)
      pickForEntity(apiEntityP);

    if (orionldState.in.omitList.items > 0)
      omitForEntity(apiEntityP);

    kjChildAdd(resultArray, apiEntityP);
  }

  PQclear(entityRes);

  orionldState.responseTree   = resultArray;
  orionldState.httpStatusCode = 200;

  return true;
}
