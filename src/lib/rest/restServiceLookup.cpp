/*
*
* Copyright 2018 Telefonica Investigacion y Desarrollo, S.A.U
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
#include "common/string.h"

#include "rest/ConnectionInfo.h"
#include "rest/RestService.h"
#include "rest/restServiceLookup.h"

#include "serviceRoutines/postDiscoverContextAvailability.h"
#include "serviceRoutines/badVerbPostOnly.h"


/* ****************************************************************************
*
* restServiceLookup -
*/
RestService* restServiceLookup(ConnectionInfo* ciP, bool* badVerbP)
{
  Verb          verb      = (*badVerbP == false)? ciP->verb : NOVERB;
  RestService*  serviceV  = restServiceVectorGet(verb);
  int           serviceIx = 0;
  bool          match     = false;

  // Split URI PATH into components
  ciP->urlComponents = stringSplit(ciP->url.c_str(), '/', ciP->urlCompV, true);

  while (serviceV[serviceIx].request != InvalidRequest)
  {
    RestService* serviceP = &serviceV[serviceIx];

    if (serviceP->components != ciP->urlComponents)
    {
      ++serviceIx;
      continue;
    }

    match = true;
    for (int compNo = 0; compNo < ciP->urlComponents; ++compNo)
    {
      const char* component = serviceP->compV[compNo].c_str();

      if ((component[0] == '*') && (component[1] == 0))
      {
        continue;
      }

      if (ciP->apiVersion == V1)
      {
        if (strcasecmp(component, ciP->urlCompV[compNo].c_str()) != 0)
        {
          match = false;
          break;
        }
      }
      else
      {
        if (strcmp(component, ciP->urlCompV[compNo].c_str()) != 0)
        {
          match = false;
          break;
        }
      }
    }

    if (match == true)
    {
      break;
    }

    ++serviceIx;
  }

  if (match == true)
  {
    if (serviceV == restBadVerbV)
    {
      ciP->httpStatusCode = SccBadVerb;
      ciP->badVerb        = true;
    }

    return &serviceV[serviceIx];
  }

  //
  // Not found?
  // Recursive call with bad verb service vector
  // But only one recursive call !
  //

  if (*badVerbP == false)
  {
    *badVerbP = true;
    return restServiceLookup(ciP, badVerbP);
  }

  return &restBadVerbV[104];  // FIXME 02: 99.9% sure we never get here, but if ... Change the 104 service for a fixed one
}
