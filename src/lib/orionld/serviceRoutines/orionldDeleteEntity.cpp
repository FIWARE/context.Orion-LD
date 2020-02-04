/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include <string>
#include <vector>

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "ngsi/ParseData.h"                                    // ParseData needed for postUpdateContext()
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/common/urlCheck.h"                           // urlCheck
#include "orionld/common/urnCheck.h"                           // urnCheck
#include "serviceRoutines/postUpdateContext.h"                 // postUpdateContext
#include "orionld/serviceRoutines/orionldDeleteEntity.h"       // Own Interface



// ----------------------------------------------------------------------------
//
// orionldDeleteEntity -
//
bool orionldDeleteEntity(ConnectionInfo* ciP)
{
  ParseData  parseData;
  Entity     entity;

  LM_T(LmtServiceRoutine, ("In orionldDeleteEntity"));

  // Check that the Entity ID is a valid URI
  char* details;

  if ((urlCheck(orionldState.wildcard[0], &details) == false) && (urnCheck(orionldState.wildcard[0], &details) == false))
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Entity ID", details);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  // Fill in entity with the entity-id from the URL
  entity.id = orionldState.wildcard[0];

  // Fill in the upcr field for postUpdateContext, with the entity and DELETE as action
  parseData.upcr.res.fill(&entity, ActionTypeDelete);

  // Call standard op postUpdateContext
  std::vector<std::string> compV;  // dum my - postUpdateContext requires this arg as its 3rd parameter
  postUpdateContext(ciP, 3, compV, &parseData);

  // Check result
  if (parseData.upcrs.res.oe.code != SccNone)
  {
    OrionldResponseErrorType eType = (parseData.upcrs.res.oe.code == SccContextElementNotFound)? OrionldResourceNotFound : OrionldBadRequestData;

    orionldErrorResponseCreate(eType, parseData.upcrs.res.oe.details.c_str(), orionldState.wildcard[0]);
    ciP->httpStatusCode = (parseData.upcrs.res.oe.code == SccContextElementNotFound)? SccContextElementNotFound : SccBadRequest;

    // Release allocated data
    parseData.upcr.res.contextElementVector.release();

    return false;
  }

  // Release allocated data
  parseData.upcr.res.contextElementVector.release();

  // HTTP Response Code is 204 - No Content
  ciP->httpStatusCode = SccNoContent;

  return true;
}
