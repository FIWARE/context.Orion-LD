/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include <stdlib.h>
#include <microhttpd.h>                                          // MHD

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjChildAdd, ...
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // Tl*
#include "orionld/mhd/mhdStart.h"                                // MhdRequestInit, MhdRequestBody, MhdRequestTreat
#include "orionld/mhd/mhdRequest.h"                              // Own interface


__thread int mhdCalls = 0;



extern MhdRequestInit  mhdRequestInitF;
extern MhdRequestBody  mhdRequestBodyF;
extern MhdRequestTreat mhdRequestTreatF;
// -----------------------------------------------------------------------------
//
// mhdRequest -
//
MHD_Result mhdRequest
(
  void*            cls,
  MHD_Connection*  connection,
  const char*      url,
  const char*      method,
  const char*      version,
  const char*      upload_data,
  size_t*          upload_data_size,
  void**           con_cls
)
{
  ++mhdCalls;
  if (mhdCalls > 50)
    KT_X(1, "More than 50 MHD calls");

  if (*con_cls == NULL)
  {
    KT_T(StRequest, "-----------------------------------------------------------------------------------------");
    KT_T(StRequest, "Incoming request: %s %s, type I (*con_cls == %p)", method, url, *con_cls);
    *con_cls = &cls;  // to "acknowledge" the first call

    return mhdRequestInitF(connection, url, method, version, con_cls);
  }
  else if (*upload_data_size != 0)
  {
    KT_T(StRequest, "Incoming request: %s %s, type II - body (*con_cls == %p)", method, url, *con_cls);
    return mhdRequestBodyF(upload_data_size, upload_data);
  }
  else
  {
    KT_T(StRequest, "Incoming request: %s %s, type III - last call (*con_cls == %p)", method, url, *con_cls);
    KT_T(StRequest, "Incoming request: %s %s", method, url);
    *upload_data_size = 0;  // Mark the request as "finished"

    int            statusCode;
    char*          response    = mhdRequestTreatF(&statusCode);   // KjNode* Would be better, but, ftClient hasn't got an orionldState ...
    int            responseLen = 0;
    MHD_Response*  r;

    if (response == NULL)
      response = (char*) "";
    else
      responseLen = strlen(response);

    KT_T(StRequest, "Response(%d bytes): '%s'", responseLen, response);
    r = MHD_create_response_from_buffer(responseLen, response, MHD_RESPMEM_MUST_COPY);
    MHD_queue_response(connection, statusCode, r);
    MHD_destroy_response(r);
  }

  return MHD_YES;
}

