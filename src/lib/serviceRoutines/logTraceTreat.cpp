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
#include <string.h>
#include <vector>

extern "C"
{
#include "ktrace/kTrace.h"
}

#include "orionld/common/orionldState.h"                     // orionldState

#include "common/statistics.h"
#include "common/clockFunctions.h"

#include "ngsi/ParseData.h"
#include "rest/ConnectionInfo.h"
#include "rest/orionLogReply.h"
#include "rest/OrionError.h"
#include "orionld/types/Verb.h"                              // verbToString
#include "serviceRoutines/logTraceTreat.h"                   // Own interface


// Store current trace level string for GET requests
static char currentTraceLevels[256] = "";


/* ****************************************************************************
*
* logTraceTreat -
*/
std::string logTraceTreat
(
  ConnectionInfo*            ciP,
  int                        components,
  std::vector<std::string>&  compV,
  ParseData*                 parseDataP
)
{
  std::string out  = "OK";
  std::string path = "";

  for (int ix = 0; ix < components; ++ix)
  {
    path += compV[ix];

    if (ix != components - 1)
      path += "/";
  }

  if ((components == 2) && (orionldState.verb == HTTP_DELETE))
  {
    ktTraceLevelSet("");
    currentTraceLevels[0] = 0;
    out = orionLogReply(ciP, "tracelevels", "all trace levels off");
  }
  else if ((components == 3) && (orionldState.verb == HTTP_DELETE))
  {
    // ktrace doesn't support removing individual trace levels
    out = orionLogReply(ciP, "tracelevels", "removing individual trace levels not supported - use DELETE /log/trace to clear all");
  }
  else if ((components == 2) && (orionldState.verb == HTTP_GET))
  {
    out = orionLogReply(ciP, "tracelevels", currentTraceLevels);
  }
  else if ((components == 3) && (orionldState.verb == HTTP_PUT))
  {
    if (strspn(compV[2].c_str(), "0123456789-,'") != strlen(compV[2].c_str()))
    {
      out = orionLogReply(ciP, "tracelevels", "poorly formatted trace level string");
      return out;
    }

    ktTraceLevelSet(compV[2].c_str());
    strncpy(currentTraceLevels, compV[2].c_str(), sizeof(currentTraceLevels) - 1);
    out = orionLogReply(ciP, "tracelevels", compV[2]);
  }
  else
  {
    OrionError error(SccBadRequest, std::string("bad URL/Verb: ") + verbToString(orionldState.verb) + " " + path);

    TIMED_RENDER(out = error.render());
  }

  return out;
}
