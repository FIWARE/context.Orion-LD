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
}

#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/pqHeader.h"                             // PGresult, PQclear
#include "orionld/payloadCheck/pCheckUri.h"                      // pCheckUri
#include "orionld/kjTree/kjSysAttrsRemove.h"                     // kjSysAttrsRemove
#include "orionld/apiModel/ntosEntity.h"                         // ntosEntity
#include "orionld/apiModel/ntocEntity.h"                         // ntocEntity
#include "orionld/apiModel/ntonEntity.h"                         // ntonEntity
#include "orionld/troe/pgTemporalEntityQuery.h"                  // pgTemporalEntityQuery
#include "orionld/troe/pgTemporalEntityBuild.h"                  // pgTemporalEntityBuild
#include "orionld/serviceRoutines/orionldGetTemporalEntity.h"    // Own Interface


extern bool troe;


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
  const char* timerel   = orionldState.uriParams.timerel;
  const char* timeAt    = orionldState.uriParams.timeAt;
  const char* endTimeAt = orionldState.uriParams.endTimeAt;

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

  // For "between", endTimeAt is required
  if (strcmp(timerel, "between") == 0 && endTimeAt == NULL)
  {
    orionldError(OrionldBadRequestData, "Missing required URI parameter 'endTimeAt' for timerel=between", "endTimeAt", 400);
    return false;
  }

  // Execute the temporal queries against TRoE
  PGresult* entityRes  = NULL;
  PGresult* attrRes    = NULL;
  PGresult* subAttrRes = NULL;

  if (pgTemporalEntityQuery(entityId, timerel, timeAt, endTimeAt, &entityRes, &attrRes, &subAttrRes) == false)
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

  // Apply output format transformation
  bool   sysAttrs = orionldState.uriParamOptions.sysAttrs;
  char*  lang     = orionldState.uriParams.lang;

  if      (orionldState.out.format == RF_SIMPLIFIED) ntosEntity(apiEntityP, lang);
  else if (orionldState.out.format == RF_CONCISE)    ntocEntity(apiEntityP, lang, sysAttrs);
  else                                               ntonEntity(apiEntityP, lang, sysAttrs);

  if (sysAttrs == false)
    kjSysAttrsRemove(apiEntityP, 2);

  orionldState.responseTree   = apiEntityP;
  orionldState.httpStatusCode = 200;

  return true;
}
