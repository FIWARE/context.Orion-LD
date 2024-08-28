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
#include <string.h>                                            // strncmp, strlen

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "orionld/types/QNode.h"                               // QNode
#include "orionld/q/qMatchCompare.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// qMatchCompare - lexical analysis of an ngsi-ld Q-filter
//
bool qMatchCompare(KjNode* lhsNode, QNode* rhs)
{
  //
  // For now, assume strings end in ".*" and do a strncmp comparison
  //
  if ((rhs->type != QNodeStringValue) && (rhs->type != QNodeRegexpValue))
    LM_RE(false, ("rhs is not a String nor a REGEX: %d", rhs->type));

  if (lhsNode->type != KjString)
    return false;

  int sLen = strlen(rhs->value.s);

  LM_T(LmtCsf, ("Comparing value of '%s' ('%s') to '%s'", lhsNode->name, lhsNode->value.s, rhs->value.s));
  if (strncmp(lhsNode->value.s, rhs->value.s, sLen - 2) == 0)
    return true;

  return false;
}
