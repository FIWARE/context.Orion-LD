/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include <string.h>                                            // strcspn, strncpy

extern "C"
{
#include "kalloc/kaAlloc.h"                                    // kaAlloc
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/uuidGenerate.h"                       // uuidGenerate
#include "orionld/common/correlatorGet.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// correlatorGet -
//
// Resolves the per-request correlator on first use and caches it. 'orionldState' is zeroed
// at the start of every request (orionldStateInit), so this produces exactly one value per
// request, shared by all writes (entity 'lastCorrelator' and all TRoE rows).
//
char* correlatorGet(void)
{
  if (orionldState.correlatorResolvedP != NULL)
    return orionldState.correlatorResolvedP;

  if ((orionldState.correlator == NULL) || (orionldState.correlator[0] == 0))
  {
    //
    // No correlator provided by the client - generate one
    //
    char* buf = kaAlloc(&orionldState.kalloc, 80);

    if (buf == NULL)
      return (char*) "";

    uuidGenerate(buf, 80, "urn:ngsi-ld:correlator:");
    orionldState.correlatorResolvedP = buf;
  }
  else
  {
    //
    // Use the client-provided correlator, but only its "root":
    //   - cut at the first ';'  -> drops a notification "; cbnotif=N" suffix
    //   - cut at the first '\'' -> the value is later embedded in a single-quoted SQL literal
    //                              (pgQuotedString does not escape), so this keeps it injection-safe
    //
    int   len = strcspn(orionldState.correlator, ";'");
    char* buf = kaAlloc(&orionldState.kalloc, len + 1);

    if (buf == NULL)
      return orionldState.correlator;

    strncpy(buf, orionldState.correlator, len);
    buf[len] = 0;
    orionldState.correlatorResolvedP = buf;
  }

  return orionldState.correlatorResolvedP;
}
