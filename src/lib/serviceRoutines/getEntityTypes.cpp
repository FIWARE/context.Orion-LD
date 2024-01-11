/*
*
* Copyright 2014 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Ken Zangelin
*/
#include <string>
#include <vector>

#include "orionld/common/orionldState.h"    // orionldState

#include "common/statistics.h"
#include "common/clockFunctions.h"
#include "rest/ConnectionInfo.h"
#include "ngsi/ParseData.h"
#include "orionTypes/EntityTypeVectorResponse.h"
#include "serviceRoutines/getEntityTypes.h"
#include "mongoBackend/mongoQueryTypes.h"



/* ****************************************************************************
*
* getEntityTypes - 
*
* GET /v1/contextTypes
*
* Payload In:  None
* Payload Out: EntityTypeVectorResponse
*
* URI parameters:
*   - attributesFormat=object
*   - collapse=true
*/
std::string getEntityTypes
(
  ConnectionInfo*            ciP,
  int                        components,
  std::vector<std::string>&  compV,
  ParseData*                 parseDataP
)
{
  EntityTypeVectorResponse  response;
  unsigned int              totalTypes   = 0;
  unsigned int*             totalTypesP  = NULL;

  bool asJsonObject = (orionldState.in.attributeFormatAsObject == true) && (orionldState.out.contentType == MT_JSON);

  response.statusCode.fill(SccOk);

  // NGSIv1 uses details=on to request count
  if (orionldState.uriParams.details == true)
  {
    totalTypesP = &totalTypes;
  }

  //
  // NOTE
  //   The last parameter for mongoEntityTypes 'bool noAttrDetail' is always
  //   set to true (meaning to skip the attribute detail) for NGSIv1 requests.
  //   The parameter is only used for NGSIv2
  //
  TIMED_MONGO(mongoEntityTypes(&response, orionldState.tenantP, ciP->servicePathV, orionldState.apiVersion, totalTypesP, true));

  std::string rendered;
  TIMED_RENDER(rendered = response.render(orionldState.apiVersion,
                                          asJsonObject,
                                          orionldState.out.contentType == MT_JSON,
                                          orionldState.uriParams.collapse));

  response.release();

  return rendered;
}
