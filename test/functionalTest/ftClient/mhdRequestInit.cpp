/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
#include <microhttpd.h>                                     // MHD_Result, MHD_Connection, MHD_get_connection_values, ...

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/kjBuilder.h"                                // kjObject, kjArray, kjString, kjChildAdd, ...
}

#include "common/orionldState.h"                            // orionldState, orionldStateInit
#include "http/verbGet.h"                                   // verbGet



// -----------------------------------------------------------------------------
//
// Thread global variables
//
__thread KjNode*        httpHeaders  = NULL;
__thread KjNode*        uriParams    = NULL;



// -----------------------------------------------------------------------------
//
// headerReceive -
//
static MHD_Result headerReceive(void* cbDataP, MHD_ValueKind kind, const char* key, const char* value)
{
  KT_V("Got an HTTP Header: '%s': '%s'", key, value);

  KjNode* headerP = kjString(NULL, key, value);
  kjChildAdd(httpHeaders, headerP);

  return MHD_YES;
}



// -----------------------------------------------------------------------------
//
// uriParamReceive -
//
MHD_Result uriParamReceive(void* cbDataP, MHD_ValueKind kind, const char* key, const char* value)
{
  KT_V("Got a URL Parameter: '%s': '%s'", key, value);

  KjNode* paramP = kjString(NULL, key, value);
  kjChildAdd(uriParams, paramP);

  return MHD_YES;
}



// -----------------------------------------------------------------------------
//
// mhdRequestInit -
//
MHD_Result mhdRequestInit(MHD_Connection* connection, const char* url, const char* method, const char* version, void** con_cls)
{
  orionldStateInit(connection);
  orionldState.verbString = (char*) method;
  orionldState.verb       = verbGet(method);
  orionldState.urlPath    = (char*) url;

  httpHeaders = kjObject(NULL, "headers");
  uriParams   = kjObject(NULL, "params");

  MHD_get_connection_values(connection, MHD_HEADER_KIND,       headerReceive,   NULL);
  MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, uriParamReceive, NULL);

  return MHD_YES;
}
