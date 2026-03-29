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

#include "orionld/types/ApiVersion.h"                            // ApiVersion
#include "orionld/common/orionldState.h"                         // orionldState

#include "common/string.h"
#include "common/defaultValues.h"
#include "common/globals.h"
#include "common/statistics.h"
#include "common/clockFunctions.h"
#include "alarmMgr/alarmMgr.h"

#include "mongoBackend/mongoUpdateContext.h"
#include "ngsi/ParseData.h"
#include "ngsi10/UpdateContextResponse.h"
#include "rest/ConnectionInfo.h"
#include "serviceRoutines/postUpdateContext.h"



/* ****************************************************************************
*
* foundAndNotFoundAttributeSeparation -
*
* Examine the response from mongo to find out what has really happened ...
*
*/
static void foundAndNotFoundAttributeSeparation(UpdateContextResponse* upcrsP, UpdateContextRequest* upcrP, ConnectionInfo* ciP)
{
  ContextElementResponseVector  notFoundV;

  for (unsigned int cerIx = 0; cerIx < upcrsP->contextElementResponseVector.size(); ++cerIx)
  {
    ContextElementResponse* cerP = upcrsP->contextElementResponseVector[cerIx];

    //
    // All attributes with found == false?
    //
    int noOfFounds    = 0;
    int noOfNotFounds = 0;

    for (unsigned int aIx = 0; aIx < cerP->contextElement.contextAttributeVector.size(); ++aIx)
    {
      if (cerP->contextElement.contextAttributeVector[aIx]->found == true)
      {
        ++noOfFounds;
      }
      else
      {
        ++noOfNotFounds;
      }
    }

    //
    // Now, if we have ONLY FOUNDS, then things stay the way they are, one response with '200 OK'
    // If we have ONLY NOT-FOUNDS, the we have one response with '404 Not Found'
    // If we have a mix, then we need to add a response for the not founds, with '404 Not Found'
    //
    if ((noOfFounds == 0) && (noOfNotFounds > 0))
    {
      if ((cerP->statusCode.code == SccOk) || (cerP->statusCode.code == SccNone))
      {
        cerP->statusCode.fill(SccContextElementNotFound, cerP->contextElement.entityId.id);
      }
    }
    else if ((noOfFounds > 0) && (noOfNotFounds > 0))
    {
      // Adding a ContextElementResponse for the 'Not-Founds'

      ContextElementResponse* notFoundCerP = new ContextElementResponse(&cerP->contextElement.entityId, NULL);

      //
      // Filling in StatusCode (SccContextElementNotFound) for NotFound
      //
      notFoundCerP->statusCode.fill(SccContextElementNotFound, cerP->contextElement.entityId.id);

      //
      // Setting StatusCode to OK for Found
      //
      cerP->statusCode.fill(SccOk);

      //
      // And, pushing to NotFound-vector
      //
      notFoundV.push_back(notFoundCerP);


      // Now moving the not-founds to notFoundCerP
      std::vector<ContextAttribute*>::iterator iter;
      for (iter = cerP->contextElement.contextAttributeVector.vec.begin(); iter < cerP->contextElement.contextAttributeVector.vec.end();)
      {
        if ((*iter)->found == false)
        {
          // 1. Push to notFoundCerP
          notFoundCerP->contextElement.contextAttributeVector.push_back(*iter);

          // 2. remove from cerP
          iter = cerP->contextElement.contextAttributeVector.vec.erase(iter);
        }
        else
        {
          ++iter;
        }
      }
    }

    //
    // Add EntityId::id to StatusCode::details if 404, but only if StatusCode::details is empty
    //
    if ((cerP->statusCode.code == SccContextElementNotFound) && (cerP->statusCode.details == ""))
    {
      cerP->statusCode.details = cerP->contextElement.entityId.id;
    }
  }

  //
  // Now add the contextElementResponses for 404 Not Found
  //
  if (notFoundV.size() != 0)
  {
    for (unsigned int ix = 0; ix < notFoundV.size(); ++ix)
    {
      upcrsP->contextElementResponseVector.push_back(notFoundV[ix]);
    }
  }


  //
  // If nothing at all in response vector, mark as not found (but not if DELETE request)
  //
  if (orionldState.verb != HTTP_DELETE)
  {
    if (upcrsP->contextElementResponseVector.size() == 0)
    {
      if (upcrsP->errorCode.code == SccOk)
      {
        upcrsP->errorCode.fill(SccContextElementNotFound, upcrP->contextElementVector[0]->entityId.id);
      }
    }
  }


  //
  // Add entityId::id to details if Not Found and only one element in response.
  // And, if 0 elements in response, take entityId::id from the request.
  //
  if (upcrsP->errorCode.code == SccContextElementNotFound)
  {
    if (upcrsP->contextElementResponseVector.size() == 1)
    {
      upcrsP->errorCode.details = upcrsP->contextElementResponseVector[0]->contextElement.entityId.id;
    }
    else if (upcrsP->contextElementResponseVector.size() == 0)
    {
      upcrsP->errorCode.details = upcrP->contextElementVector[0]->entityId.id;
    }
  }
}



/* ****************************************************************************
*
* attributesToNotFound - mark all attributes with 'found=false'
*/
static void attributesToNotFound(UpdateContextRequest* upcrP)
{
  for (unsigned int ceIx = 0; ceIx < upcrP->contextElementVector.size(); ++ceIx)
  {
    ContextElement* ceP = upcrP->contextElementVector[ceIx];

    for (unsigned int aIx = 0; aIx < ceP->contextAttributeVector.size(); ++aIx)
    {
      ContextAttribute* aP = ceP->contextAttributeVector[aIx];

      aP->found = false;
    }
  }
}



/* ****************************************************************************
*
* postUpdateContext -
*
* POST /v1/updateContext
* POST /ngsi10/updateContext
*
* Payload In:  UpdateContextRequest
* Payload Out: UpdateContextResponse
*/
std::string postUpdateContext
(
  ConnectionInfo*            ciP,
  int                        components,
  std::vector<std::string>&  compV,
  ParseData*                 parseDataP,
  Ngsiv2Flavour              ngsiV2Flavour
)
{
  UpdateContextResponse*  upcrsP = &parseDataP->upcrs.res;
  UpdateContextRequest*   upcrP  = &parseDataP->upcr.res;
  std::string             answer;

  bool asJsonObject = (orionldState.in.attributeFormatAsObject == true) && (orionldState.out.contentType == MT_JSON);

  //
  // 01. Check service-path consistency
  //
  // If more than ONE service-path is input, an error is returned as response.
  // If ONE service-path is issued and that service path is "", then the default service-path is used.
  // Note that by construction servicePath cannot have 0 elements
  // After these checks, the service-path is checked to be 'correct'.
  //
  if (ciP->servicePathV.size() > 1)
  {
    upcrsP->errorCode.fill(SccBadRequest, "more than one service path in context update request");
    alarmMgr.badInput(orionldState.clientIp, "more than one service path for an update request");

    TIMED_RENDER(answer = upcrsP->render(orionldState.apiVersion, asJsonObject));
    upcrP->release();

    return answer;
  }
  else if (ciP->servicePathV[0] == "")
  {
    ciP->servicePathV[0] = SERVICE_PATH_ROOT;
  }

  std::string res = servicePathCheck(ciP->servicePathV[0].c_str());
  if (res != "OK")
  {
    upcrsP->errorCode.fill(SccBadRequest, res);

    TIMED_RENDER(answer = upcrsP->render(orionldState.apiVersion, asJsonObject));

    upcrP->release();
    return answer;
  }


  //
  // 02. Send the request to mongoBackend/mongoUpdateContext
  //
  upcrsP->errorCode.fill(SccOk);
  attributesToNotFound(upcrP);

  HttpStatusCode httpStatusCode;
  TIMED_MONGO(httpStatusCode = mongoUpdateContext(upcrP,
                                                  upcrsP,
                                                  orionldState.tenantP,
                                                  ciP->servicePathV,
                                                  orionldState.in.xAuthToken,
                                                  orionldState.correlator,
                                                  orionldState.attrsFormat,
                                                  orionldState.apiVersion,
                                                  ngsiV2Flavour));

  if (orionldState.httpStatusCode != SccCreated)
    orionldState.httpStatusCode = httpStatusCode;

  foundAndNotFoundAttributeSeparation(upcrsP, upcrP, ciP);


  TIMED_RENDER(answer = upcrsP->render(orionldState.apiVersion, asJsonObject));

  upcrP->release();
  return answer;
}
