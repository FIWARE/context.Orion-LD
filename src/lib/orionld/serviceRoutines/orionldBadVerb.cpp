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
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/types/OrionldHeader.h"                       // orionldHeaderAdd
#include "orionld/rest/orionldServiceInit.h"                   // orionldRestServiceV
#include "orionld/rest/orionldServiceLookup.h"                 // orionldServiceLookup
#include "orionld/serviceRoutines/orionldBadVerb.h"            // Own Interface



// -----------------------------------------------------------------------------
//
// orionldBadVerb -
//
bool orionldBadVerb(void)
{
  uint16_t  bitmask = 0;

  //
  // There are nine verbs/methods, but only GET, PUT, POST, PATCH and DELETE are supported by ORIONLD
  // This loop looks up the URL PATH for each "orionld-valid" verb and keeps a bitmask of the hits
  //
  for (uint16_t verbNo = 0; verbNo <= PATCH; verbNo++)  // 0:GET, 1:PUT, 2:POST, 3:DELETE, 4:PATCH
  {
    if (orionldServiceLookup(&orionldRestServiceV[verbNo]) != NULL)
    {
      bitmask |= (1 << verbNo);
    }
  }

  if (bitmask == 0)
    return false;

  char allowValue[128];

  allowValue[0] = 0;

  if (bitmask & (1 << GET))    strcat(allowValue, ",GET");
  if (bitmask & (1 << PUT))    strcat(allowValue, ",PUT");
  if (bitmask & (1 << POST))   strcat(allowValue, ",POST");
  if (bitmask & (1 << DELETE)) strcat(allowValue, ",PATCH");
  if (bitmask & (1 << PATCH))  strcat(allowValue, ",DELETE");

  orionldHeaderAdd(&orionldState.out.headers, HttpAllow, (char*) &allowValue[1], 0);  // Skipping first comma of 'allowValue'

  return true;
}
