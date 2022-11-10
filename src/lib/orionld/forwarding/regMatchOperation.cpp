/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "orionld/regCache/RegCache.h"                           // RegCacheItem
#include "orionld/forwarding/FwdOperation.h"                     // FwdOperation
#include "orionld/kjTree/kjTreeLog.h"                            // kjTreeLog
#include "orionld/forwarding/regMatchOperation.h"                // Own interface



// -----------------------------------------------------------------------------
//
// regMatchOperation -
//
bool regMatchOperation(RegCacheItem* regP, FwdOperation op)
{
  KjNode* operations = kjLookup(regP->regTree, "operations");

  if (operations == NULL)
  {
    LM_W(("Registration without operations - that can't be"));
    kjTreeLog(regP->regTree, "Registration in reg-cache");
    return false;
  }

  uint64_t opShifted = (1L << op);
  return ((regP->opMask & opShifted) == opShifted);
}
