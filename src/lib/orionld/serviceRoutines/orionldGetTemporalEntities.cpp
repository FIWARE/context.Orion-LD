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
}

#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/OrionldHeader.h"                         // orionldHeaderAdd, HttpResultsCount
#include "orionld/types/OrionldGeoInfo.h"                        // OrionldGeoInfo
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/pqHeader.h"                             // PGresult, PQclear, PQntuples, PQgetvalue
#include "orionld/kjTree/kjSysAttrsRemove.h"                     // kjSysAttrsRemove
#include "orionld/context/orionldEntityCompact.h"                // orionldEntityCompact
#include "orionld/apiModel/ntosEntity.h"                         // ntosEntity
#include "orionld/apiModel/ntocEntity.h"                         // ntocEntity
#include "orionld/apiModel/ntonEntity.h"                         // ntonEntity
#include "orionld/common/pick.h"                                 // pickForEntity
#include "orionld/common/omit.h"                                 // omitForEntity
#include "orionld/common/datasetTemporalEntityFix.h"             // datasetTemporalEntityFix
#include "orionld/common/temporalValuesTransform.h"              // temporalValuesTransform
#include "orionld/troe/qTreeToSql.h"                             // troeQStringToSql
#include "orionld/common/aggregatedValuesTransform.h"            // aggregatedValuesTransform
#include "orionld/troe/geoFilterToSql.h"                         // geoFilterToSql
#include "orionld/payloadCheck/pCheckGeo.h"                      // pCheckGeo
#include "orionld/troe/pgTemporalEntitiesQuery.h"                // pgTemporalEntitiesQuery
#include "orionld/troe/pgTemporalEntityQuery.h"                  // pgTemporalEntityQuery
#include "orionld/troe/pgTemporalEntityBuild.h"                  // pgTemporalEntityBuild
#include "orionld/serviceRoutines/orionldGetTemporalEntities.h"  // Own Interface


extern bool troe;



// -----------------------------------------------------------------------------
//
// orionldGetTemporalEntities -
//
bool orionldGetTemporalEntities(void)
{
  if (troe == false)
  {
    orionldError(OrionldOperationNotSupported, "TRoE is not enabled - temporal operations require -troe flag", orionldState.serviceP->url, 501);
    orionldState.noLinkHeader = true;
    return false;
  }

  // Extract URI parameters
  const char* timerel      = orionldState.uriParams.timerel;
  const char* timeAt       = orionldState.uriParams.timeAt;
  const char* endTimeAt    = orionldState.uriParams.endTimeAt;
  const char* timeproperty = orionldState.uriParams.timeproperty;
  int         lastN        = orionldState.uriParams.lastN;
  int         limit        = orionldState.uriParams.limit;
  int         offset       = orionldState.uriParams.offset;

  // timerel and timeAt are mandatory for collection queries (clause 6.18.3.2)
  if (timerel == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter", "timerel", 400);
    return false;
  }

  if (timeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter", "timeAt", 400);
    return false;
  }

  // Validate timerel value
  if (strcmp(timerel, "before") != 0 && strcmp(timerel, "after") != 0 && strcmp(timerel, "between") != 0)
  {
    orionldError(OrionldBadRequestData, "Invalid value for URI parameter 'timerel'", timerel, 400);
    return false;
  }

  if (strcmp(timerel, "between") == 0 && endTimeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter 'endTimeAt' for timerel=between", "endTimeAt", 400);
    return false;
  }

  // type OR attrs must be present (clause 6.18.3.2)
  if (orionldState.in.typeList.items == 0 && orionldState.in.attrList.items == 0)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter", "type or attrs must be provided", 400);
    return false;
  }

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
  // Parse q-parameter if present
  //
  const char* qFilter = NULL;
  if (orionldState.uriParams.q != NULL)
  {
    qFilter = troeQStringToSql(orionldState.uriParams.q);
    if (qFilter == NULL)
      return false;  // troeQStringToSql already set the error
  }

  //
  // Parse geo parameters if present
  //
  const char*    geoFilter = NULL;
  OrionldGeoInfo geoInfo;
  if (orionldState.uriParams.geometry != NULL)
  {
    if (pCheckGeo(&geoInfo, orionldState.uriParams.geometry, orionldState.uriParams.georel,
                  orionldState.uriParams.coordinates, orionldState.uriParams.geoproperty) == false)
      return false;

    geoFilter = geoFilterToSql(&geoInfo);
  }

  //
  // Step 1: Discover matching entities with pagination
  //
  long long  count     = 0;
  long long* countP    = orionldState.uriParams.count ? &count : NULL;
  PGresult*  entityRes = NULL;

  if (pgTemporalEntitiesQuery(&orionldState.in.typeList, &orionldState.in.idList,
                              orionldState.uriParams.idPattern,
                              timerel, timeAt, endTimeAt, qFilter, geoFilter,
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
                              &orionldState.in.attrList, lastN,
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

    // Post-processing (same as single-entity handler)
    orionldEntityCompact(apiEntityP, orionldState.contextP);

    if (orionldState.uriParams.datasetId != NULL)
      datasetTemporalEntityFix(apiEntityP);

    if      (orionldState.out.format == RF_SIMPLIFIED) ntosEntity(apiEntityP, lang);
    else if (orionldState.out.format == RF_CONCISE)    ntocEntity(apiEntityP, lang, sysAttrs);
    else                                               ntonEntity(apiEntityP, lang, sysAttrs);

    if (orionldState.uriParams.format != NULL && strcmp(orionldState.uriParams.format, "temporalValues") == 0)
      temporalValuesTransform(apiEntityP);

    if (orionldState.uriParams.aggrMethods != NULL)
      aggregatedValuesTransform(apiEntityP, orionldState.uriParams.aggrMethods,
                                orionldState.uriParams.aggrPeriodDuration,
                                timeAt, endTimeAt);

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
