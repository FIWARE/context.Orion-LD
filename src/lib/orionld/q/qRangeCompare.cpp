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
#include "kjson/KjNode.h"                                      // KjNode
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "orionld/types/QNode.h"                               // QNode
#include "orionld/common/dateTime.h"                           // dateTimeFromString
#include "orionld/q/qRangeCompare.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// qRangeCompare - lexical analysis of an ngsi-ld Q-filter
//
bool qRangeCompare(KjNode* lhsNode, QNode* rhs, bool isTimestamp)
{
  QNode* low  = rhs->value.children;

  if (low == NULL)
    return false;

  QNode* high = low->next;

  if (high == NULL)
    return false;

  if (isTimestamp)
  {
    char errorString[256];

    double lhsTimestamp  = dateTimeFromString(lhsNode->value.s, errorString, sizeof(errorString));
    double lowTimestamp  = (low->type == QNodeFloatValue)? low->value.f :  dateTimeFromString(low->value.s, errorString, sizeof(errorString));
    double highTimestamp = dateTimeFromString(high->value.s, errorString, sizeof(errorString));

    if ((lhsTimestamp < 0) || (lowTimestamp < 0) || (highTimestamp < 0))
      LM_RE(false, ("Invalid ISO8601 timestamp: %s", errorString));

    if ((lhsTimestamp >= lowTimestamp) && (lhsTimestamp <= highTimestamp))
      return true;

    return false;
  }

  if (lhsNode->type == KjInt)
  {
    if (low->type == QNodeIntegerValue)
    {
      if (lhsNode->value.i < low->value.i)
        return false;
    }
    else if (low->type == QNodeFloatValue)
    {
      if ((double) lhsNode->value.i < low->value.f)
        return false;
    }
    else
      return false;  // type mismatch

    if (high->type == QNodeIntegerValue)
    {
      if (lhsNode->value.i > high->value.i)
        return false;
    }
    else if (high->type == QNodeFloatValue)
    {
      if ((double) lhsNode->value.i > high->value.f)
        return false;
    }
    else
      return false;  // type mismatch

    return true;
  }

  if (lhsNode->type == KjFloat)
  {
    if (low->type == QNodeFloatValue)
    {
      if (lhsNode->value.f < low->value.f)
        return false;
    }
    else if (low->type == QNodeIntegerValue)
    {
      if (lhsNode->value.f < (double) low->value.i)
        return false;
    }
    else
      return false;  // type mismatch

    if (high->type == QNodeFloatValue)
    {
      if (lhsNode->value.f > high->value.f)
        return false;
    }
    else if (high->type == QNodeIntegerValue)
    {
      if (lhsNode->value.f > (double) high->value.i)
        return false;
    }
    else
      return false;  // type mismatch

    return true;
  }

  if (lhsNode->type == KjString)
  {
    if (low->type == QNodeStringValue)
    {
      if (strcmp(lhsNode->value.s, low->value.s)  < 0)
        return false;
      if (strcmp(lhsNode->value.s, high->value.s) > 0)
        return false;

      return true;
    }

    return false;  // type mismatch
  }

  // Timestamp!

  return false;  // RANGES operate only on Numbers, Strings and Timestamps
}
