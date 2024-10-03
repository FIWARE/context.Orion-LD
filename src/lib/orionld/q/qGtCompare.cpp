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

#include "orionld/common/dateTime.h"                           // dateTimeFromString
#include "orionld/types/QNode.h"                               // QNode



// -----------------------------------------------------------------------------
//
// qGtCompare -
//
bool qGtCompare(KjNode* lhsNode, QNode* rhs, bool isTimestamp)
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
      if (rhs->value.i < ts)
        return true;

      return false;
    }
    else if (rhs->type == QNodeFloatValue)
    {
      if (rhs->value.f < timestamp)
        return true;

      return false;
    }
  }

  if (lhsNode->type == KjInt)
  {
    if      (rhs->type == QNodeIntegerValue) return (lhsNode->value.i > rhs->value.i);
    else if (rhs->type == QNodeFloatValue)   return (lhsNode->value.i > rhs->value.f);
  }
  else if (lhsNode->type == KjFloat)
  {
    if      (rhs->type == QNodeIntegerValue) return (lhsNode->value.f > rhs->value.i);
    else if (rhs->type == QNodeFloatValue)   return (lhsNode->value.f > rhs->value.f);
  }
  else if (lhsNode->type == KjString)
  {
    if (rhs->type == QNodeStringValue)
      return (strcmp(lhsNode->value.s, rhs->value.s) > 0);  // "> 0" means first arg > second arg to strcmp
  }
  else if (lhsNode->type == KjBoolean)  // true > false ... ?
  {
    if (rhs->type == QNodeTrueValue)       return false;
    if (rhs->type == QNodeFalseValue)      return (lhsNode->value.b == true);
  }

  return false;
}
