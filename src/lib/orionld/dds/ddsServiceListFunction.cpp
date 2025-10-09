/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "ktrace/ktTraceLevelCheck.h"                       // ktTraceLevelCheck
}

#include "orionld/types/DdsService.h"                       // DdsService
#include "orionld/common/orionldState.h"                    // ddsServices
#include "orionld/common/fileName.h"                        // fileName



// -----------------------------------------------------------------------------
//
// ddsServiceListFunction
//
void ddsServiceListFunction(const char* path, int lineNo, const char* functionName, int traceLevel)
{
  if (ktTraceLevelCheck(traceLevel) == false)
    return;

  char* fileNameOnly = fileName(path);
  int   count = 0;

  for (DdsService* sP = ddsServices; sP != NULL; sP = sP->next)
  {
    ++count;
  }
  ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "There are %d DDS Services known to the system", count);

  for (DdsService* sP = ddsServices; sP != NULL; sP = sP->next)
  {
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "--------------------------------------------------------------------------------");
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "Service Name:   '%s'", sP->name);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "Request Type:   '%s'", sP->requestType);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "Reply Type:     '%s'", sP->replyType);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "Entity Id:      '%s'", sP->entityId);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "Entity Type:    '%s'", sP->entityType);
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "Attribute Name: '%s'", sP->attributeName);
  }

  if (count > 0)
    ktOut(fileNameOnly, lineNo, functionName, 'T', traceLevel, "--------------------------------------------------------------------------------");
}
