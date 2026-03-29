/*
*
* Copyright 2013 Telefonica Investigacion y Desarrollo, S.A.U
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

extern "C"
{
#include "ktrace/kTrace.h"
}
#include "orionld/common/traceLevels.h"

#include "orionld/types/ApiVersion.h"                          // ApiVersion
#include "orionld/types/OrionldHeader.h"                       // orionldHeaderAdd
#include "orionld/common/orionldState.h"                       // orionldState


#include "common/string.h"
#include "common/globals.h"
#include "common/statistics.h"
#include "common/clockFunctions.h"
#include "alarmMgr/alarmMgr.h"
#include "mongoBackend/mongoQueryContext.h"
#include "ngsi/ParseData.h"
#include "ngsi10/QueryContextRequest.h"
#include "ngsi10/QueryContextResponse.h"
#include "rest/ConnectionInfo.h"
#include "rest/OrionError.h"
#include "serviceRoutines/postQueryContext.h"



/* ****************************************************************************
*
* postQueryContext -
*/
std::string postQueryContext
(
  ConnectionInfo*            ciP,
  int                        components,
  std::vector<std::string>&  compV,
  ParseData*                 parseDataP
)
{
  //
  // Convops calling this routine may need the response in digital
  // So, the digital response is passed back in parseDataP->qcrs.res.
  //
  QueryContextResponse*       qcrsP = &parseDataP->qcrs.res;
  QueryContextRequest*        qcrP  = &parseDataP->qcr.res;
  std::string                 answer;
  long long                   count = 0;
  long long*                  countP = NULL;

  bool asJsonObject = (orionldState.in.attributeFormatAsObject == true) && (orionldState.out.contentType == MT_JSON);

  //
  // 00. Count or not count? That is the question ...
  //
  // For API version 1, if the URI parameter 'details' is set to 'on', then the total of local
  // entities is returned in the errorCode of the payload.
  //
  // In API version 2, this has changed completely. Here, the total count of local entities is returned
  // if the URI parameter 'count' is set to 'true', and it is returned in the HTTP header Fiware-Total-Count.
  //
  if (((orionldState.apiVersion == API_VERSION_NGSI_V2) || (orionldState.apiVersion == API_VERSION_NGSILD_V1)) && (orionldState.uriParams.count == true))
  {
    countP = &count;
  }


  //
  // 01. Call mongoBackend/mongoQueryContext
  //
  qcrsP->errorCode.fill(SccOk);

  TIMED_MONGO(orionldState.httpStatusCode = mongoQueryContext(qcrP,
                                                              qcrsP,
                                                              orionldState.tenantP,
                                                              ciP->servicePathV,
                                                              countP,
                                                              orionldState.apiVersion));

  if (qcrsP->errorCode.code == SccBadRequest)
  {
    // Bad Input detected by Mongo Backend - request ends here !
    OrionError oe(qcrsP->errorCode);

    TIMED_RENDER(answer = oe.render());
    qcrP->release();
    return answer;
  }


  //
  // If API version 2, add count, if asked for, in HTTP header Fiware-Total-Count
  //
  if (((orionldState.apiVersion == API_VERSION_NGSI_V2) || (orionldState.apiVersion == API_VERSION_NGSILD_V1)) && (countP != NULL))
  {
    OrionldHeaderType headerType = (orionldState.apiVersion == API_VERSION_NGSILD_V1)? HttpResultsCount : HttpNgsiv2Count;

    orionldHeaderAdd(&orionldState.out.headers, headerType, NULL, *countP);
  }



  TIMED_RENDER(answer = qcrsP->render(orionldState.apiVersion, asJsonObject));

  qcrP->release();
  return answer;
}
