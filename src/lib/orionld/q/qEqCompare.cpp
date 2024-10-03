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
#include <string.h>                                            // strcmp

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
}

#include "orionld/types/QNode.h"                               // QNode
#include "orionld/common/dateTime.h"                           // dateTimeFromString



// -----------------------------------------------------------------------------
//
// qEqCompare - lexical analysis of an ngsi-ld Q-filter
//
bool qEqCompare(KjNode* lhsNode, QNode* rhs, bool isTimestamp)
{
  //
  // Might be a timestamp ... (observedAt, modifiedAt, or createdAt)
  //
  if (isTimestamp == true)
  {
    // lhsNode must be a string, and a valid ISO8601 at that
    if (lhsNode->type != KjString)
      return false;

    char   errorString[256];
    double timestamp = dateTimeFromString(lhsNode->value.s, errorString, sizeof(errorString));

    if (rhs->type == QNodeIntegerValue)
    {
      long long ts = (long long) timestamp;
      if (rhs->value.i == ts)
        return true;

      return false;
    }
    else if (rhs->type == QNodeFloatValue)
    {
      if (rhs->value.f == timestamp)
        return true;

      return false;
    }
  }

  if (lhsNode->type == KjInt)
  {
    if      (rhs->type == QNodeIntegerValue) return (lhsNode->value.i == rhs->value.i);
    else if (rhs->type == QNodeFloatValue)   return (lhsNode->value.i == rhs->value.f);
  }
  else if (lhsNode->type == KjFloat)
  {
    double margin = 0.000001;  // What margin should I use?

    if (rhs->type == QNodeIntegerValue)
    {
      if ((lhsNode->value.f - margin < rhs->value.i) && (lhsNode->value.f + margin > rhs->value.i))
        return true;
    }
    else if (rhs->type == QNodeFloatValue)
    {
      if ((lhsNode->value.f - margin < rhs->value.f) && (lhsNode->value.f + margin > rhs->value.f))
        return true;
    }
  }
  else if (lhsNode->type == KjString)
  {
    if (rhs->type == QNodeStringValue)
      return (strcmp(lhsNode->value.s, rhs->value.s) == 0);
  }
  else if (lhsNode->type == KjBoolean)
  {
    if (rhs->type == QNodeTrueValue)       return (lhsNode->value.b == true);
    if (rhs->type == QNodeFalseValue)      return (lhsNode->value.b == false);
  }

  return false;
}
