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

extern "C"
{
#include "ktrace/kTrace.h"
}

#include "orionld/common/traceLevels.h"

#include "orionld/types/ApiVersion.h"                          // ApiVersion
#include "orionld/common/orionldState.h"                       // orionldState

#include "common/MimeType.h"
#include "common/limits.h"
#include "ngsi/StatusCode.h"
#include "metricsMgr/metricsMgr.h"

#include "ngsi10/QueryContextResponse.h"
#include "ngsi10/UpdateContextResponse.h"

#include "rest/HttpHeaders.h"                                    // HTTP_* defines
#include "rest/rest.h"
#include "rest/ConnectionInfo.h"
#include "rest/uriParamNames.h"
#include "rest/HttpStatusCode.h"
#include "rest/mhd.h"
#include "rest/OrionError.h"
#include "rest/restReply.h"



/* ****************************************************************************
*
* restReply -
*/
void restReply(ConnectionInfo* ciP, const char* answer)
{
  MHD_Response*  response;
  int            answerLen = (answer != NULL)? strlen(answer) : 0;
  char*          spath     = NULL;

  if ((orionldState.apiVersion != API_VERSION_NGSILD_V1) && (ciP->servicePathV.size() > 0))
    spath = (char*) ciP->servicePathV[0].c_str();

  KT_T(KtResponse, "Response Body: '%s'", (answer != NULL)? answer : "None");
  KT_T(KtResponse, "Response Code:  %d", orionldState.httpStatusCode);

  response = MHD_create_response_from_buffer(answerLen, (char*) answer, MHD_RESPMEM_MUST_COPY);
  bool metrics = (orionldState.apiVersion != API_VERSION_NGSILD_V1) && metricsMgr.isOn();
  if (!response)
  {
    if (metrics == true)
      metricsMgr.add(orionldState.tenantP->tenant, spath, METRIC_TRANS_IN_ERRORS, 1);

    KT_E("Runtime Error (MHD_create_response_from_buffer FAILED)");

    if (orionldState.responsePayloadAllocated == true)
    {
      free(orionldState.responsePayload);
      orionldState.responsePayload = NULL;
    }

    return;
  }

  if ((metrics == true) && (answerLen > 0))
    metricsMgr.add(orionldState.tenantP->tenant, spath, METRIC_TRANS_IN_RESP_SIZE, answerLen);

  //
  // Get the HTTP headers from orionldState.out.headers
  //
  for (int ix = 0; ix < orionldState.out.headers.ix; ix++)
  {
    OrionldHeader* hP = &orionldState.out.headers.headerV[ix];

    MHD_add_response_header(response, orionldHeaderName[hP->type], hP->sValue);
  }

  if ((answer != NULL) && (*answer != 0))
  {
    char* contentType = (char*) "application/json";
    if (orionldState.apiVersion == API_VERSION_NGSILD_V1)
    {
      if      (answerLen <= 2)                             contentType = (char*) "application/json";
      else if (orionldState.httpStatusCode  >= 400)        contentType = (char*) "application/json";
      else if (orionldState.out.contentType == MT_JSONLD)  contentType = (char*) "application/ld+json";
      else if (orionldState.out.contentType == MT_GEOJSON) contentType = (char*) "application/geo+json";
    }
    else
    {
      if (orionldState.out.contentType == MT_TEXT)
        contentType = (char*) "text/plain";
    }

    MHD_add_response_header(response, HTTP_CONTENT_TYPE, contentType);
  }

  // Check if CORS is enabled, the Origin header is present in the request and the response is not a bad verb response
  if (orionldState.apiVersion != API_VERSION_NGSILD_V1)
  {
    if ((corsEnabled == true) && (orionldState.in.origin != NULL) && (orionldState.httpStatusCode != SccBadVerb))
    {
      // Only GET method is supported for V1 API
      if ((orionldState.apiVersion == API_VERSION_NGSI_V2) || (orionldState.apiVersion == API_VERSION_NGSI_V1 && orionldState.verb == HTTP_GET))
      {
        //
        // If any origin is allowed, the header is sent always with "any" as value
        // If a specific origin is allowed, the header is only sent if the origins match
        //
        char* allowOrigin  = NULL;

        if      (strcmp(corsOrigin, "__ALL")                == 0)  allowOrigin = (char*) "*";
        else if (strcmp(orionldState.in.origin, corsOrigin) == 0)  allowOrigin = corsOrigin;

        // If the origin is not allowed, no headers are added to the response
        if (allowOrigin != NULL)
        {
          MHD_add_response_header(response, HTTP_ACCESS_CONTROL_ALLOW_ORIGIN,   allowOrigin);
          MHD_add_response_header(response, HTTP_ACCESS_CONTROL_EXPOSE_HEADERS, CORS_EXPOSED_HEADERS);

          if (orionldState.verb == HTTP_OPTIONS)
          {
            char maxAge[16];
            snprintf(maxAge, sizeof(maxAge), "%d", corsMaxAge);

            MHD_add_response_header(response, HTTP_ACCESS_CONTROL_ALLOW_HEADERS, CORS_ALLOWED_HEADERS);
            MHD_add_response_header(response, HTTP_ACCESS_CONTROL_MAX_AGE,       maxAge);
          }
        }
      }
    }
  }

  MHD_queue_response(orionldState.mhdConnection, orionldState.httpStatusCode, response);
  MHD_destroy_response(response);

  if ((orionldState.responsePayloadAllocated == true) && (orionldState.responsePayload != NULL))
  {
    free(orionldState.responsePayload);
    orionldState.responsePayload = NULL;
  }
}



/* ****************************************************************************
*
* restErrorReplyGet -
*
* This function renders an error reply depending on the type of the request (ciP->restServiceP->request).
*
* The function is called from more than one place, especially from
* restErrorReply(), but also from where the payload type is matched against the request URL.
* Where the payload type is matched against the request URL, the incoming 'request' is a
* request and not a response.
*/
void restErrorReplyGet(ConnectionInfo* ciP, int statusCode, const std::string& details, std::string* outStringP)
{
  StatusCode  errorCode((HttpStatusCode) statusCode, details, "errorCode");

  orionldState.httpStatusCode = SccOk;

  if (ciP->restServiceP->request == QueryContext)
  {
    QueryContextResponse  qcr(errorCode);
    bool                  asJsonObject = (orionldState.in.attributeFormatAsObject == true) && ((orionldState.out.contentType == MT_JSON) || (orionldState.out.contentType == MT_JSONLD));
    *outStringP = qcr.render(orionldState.apiVersion, asJsonObject);
  }
  else if (ciP->restServiceP->request == UpdateContext)
  {
    UpdateContextResponse ucr(errorCode);
    bool asJsonObject = (orionldState.in.attributeFormatAsObject == true) && ((orionldState.out.contentType == MT_JSON) || (orionldState.out.contentType == MT_JSONLD));
    *outStringP = ucr.render(orionldState.apiVersion, asJsonObject);
  }
  else
  {
    OrionError oe(errorCode);

    KT_E("Unknown request type: '%d'", ciP->restServiceP->request);
    orionldState.httpStatusCode = oe.code;
    *outStringP = oe.setStatusCodeAndSmartRender(orionldState.apiVersion, &orionldState.httpStatusCode);
  }
}
