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
#include <string.h>                                              // strlen

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_T
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kprom/kprom.h"                                         // kpromMetrics, kpromRender, kpromRenderSize
}

#include "orionld/types/OrionldMimeType.h"                       // MimeType, MT_TEXT
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KtPrometheus
#include "orionld/serviceRoutines/orionldGetMetrics.h"           // Own Interface



// ----------------------------------------------------------------------------
//
// orionldGetMetrics -
//
bool orionldGetMetrics(void)
{
  KT_T(55, "Entering orionldGetMetrics - first line");
  KT_T(55, "orionldState at %p", &orionldState);
  KT_T(55, "acceptTextPlain: %s", orionldState.in.acceptTextPlain ? "true" : "false");
  KT_T(55, "Setting noLinkHeader");
  orionldState.noLinkHeader = true;
  KT_T(55, "noLinkHeader set");

  //
  // Check Accept header - if text/plain requested, return Prometheus format
  // Otherwise return JSON (default)
  //
  if (orionldState.in.acceptTextPlain == true)
  {
    KT_T(KtPrometheus, "Returning Prometheus text format");

    // Calculate size needed for Prometheus text format
    int   bufSize = kpromRenderSize() + 1;  // +1 for null terminator
    char* buf;

    // Use kalloc if it fits, otherwise malloc
    if (bufSize < (int) orionldState.kalloc.bytesLeft)
    {
      buf = kaAlloc(&orionldState.kalloc, bufSize);
      KT_T(KtPrometheus, "Allocated %d bytes from kalloc", bufSize);
    }
    else
    {
      buf = (char*) malloc(bufSize);
      if (buf == NULL)
      {
        KT_E("Out of memory allocating metrics buffer (%d bytes)", bufSize);
        orionldState.httpStatusCode = 500;
        return false;
      }
      orionldStateDelayedFreeEnqueue(buf);
      KT_T(KtPrometheus, "Allocated %d bytes from malloc", bufSize);
    }

    kpromRender(buf, bufSize);
    KT_T(KtPrometheus, "kpromRender done");

    orionldState.responsePayload = buf;
    orionldState.out.contentType = MT_TEXT;
  }
  else
  {
    KT_T(KtPrometheus, "Returning JSON format");

    // JSON format (default)
    orionldState.responseTree = kpromMetrics(orionldState.kjsonP);
    KT_T(KtPrometheus, "kpromMetrics done, responseTree: %p", orionldState.responseTree);
  }

  KT_T(KtPrometheus, "Leaving orionldGetMetrics");
  return true;
}
