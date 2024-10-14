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
extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/kjFree.h"                                   // kjFree
#include "kjson/kjBuilder.h"                                // kjArray
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // Trace levels for ktrace



// FIXME: put in header file and include
extern KjNode*  dumpArray;



// -----------------------------------------------------------------------------
//
// deleteDump -
//
KjNode* deleteDump(int* statusCodeP)
{
  KT_T(StRequest, "Resetting HTTP Dump");

  if (dumpArray != NULL)
    kjFree(dumpArray);

  dumpArray = kjArray(NULL, "dumpArray");

  *statusCodeP = 204;
  KT_T(StRequest, "Reset HTTP Dump");

  return NULL;
}
