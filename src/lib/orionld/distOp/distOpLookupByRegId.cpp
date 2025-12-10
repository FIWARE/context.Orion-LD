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
#include <unistd.h>                                              // NULL
#include <string.h>                                              // strcmp
#include <curl/curl.h>                                           // curl

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/types/DistOp.h"                                // DistOp
#include "orionld/distOp/distOpLookupByRegId.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// distOpLookupByRegId -
//
DistOp* distOpLookupByRegId(DistOp* distOpList, const char* regId)
{
  DistOp* distOpP = distOpList;
  bool    local   = strcmp(regId, "@none") == 0;

  KT_T(KtDistOpList, "Looking for DistOp '%s'", regId);
  while (distOpP != NULL)
  {
    if ((local == true) && (distOpP->regP == NULL))
    {
      KT_T(KtDistOpList, "Found DistOp '%s'", regId);
      return distOpP;
    }

    if ((distOpP->regP != NULL) && (distOpP->regP->regId != NULL))
    {
      KT_T(KtDistOpList, "Comparing with '%s'", distOpP->regP->regId);
      if (strcmp(distOpP->regP->regId, regId) == 0)
      {
        KT_T(KtDistOpList, "Found DistOp for reg '%s'", regId);
        return distOpP;
      }
    }

    distOpP = distOpP->next;
  }

  KT_T(KtDistOpList, "DistOp for reg '%s' NOT FOUND", regId);
  return NULL;
}
