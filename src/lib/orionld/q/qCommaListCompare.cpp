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

#include "logMsg/logMsg.h"                                     // LM_*

#include "orionld/common/dateTime.h"                           // dateTimeFromString
#include "orionld/types/QNode.h"                               // QNode



// -----------------------------------------------------------------------------
//
// intComparison
//
static bool intComparison(long long lhs, QNode* rhs)
{
  if      (rhs->type == QNodeIntegerValue)  return lhs == rhs->value.i;
  else if (rhs->type == QNodeFloatValue)    return ((double) lhs) == rhs->value.f;

  return false;
}



// -----------------------------------------------------------------------------
//
// floatComparison
//
static bool floatComparison(KjNode* lhsP, QNode* rhs, bool isTimestamp)
{
  if (isTimestamp == false)
  {
    double lhs = lhsP->value.f;

    if      (rhs->type == QNodeFloatValue)    return lhs == rhs->value.f;  // precision for float comparison??
    else if (rhs->type == QNodeIntegerValue)  return lhs == (double) rhs->value.i;
    else                                      return false;
  }

  if (rhs->type == QNodeStringValue)
  {
    char   errorString[256];
    double rhsTimestamp = dateTimeFromString(rhs->value.s, errorString, sizeof(errorString));

    if (lhsP->value.f == rhsTimestamp)
      return true;
  }

  return false;
}


// -----------------------------------------------------------------------------
//
// boolComparison
//
static bool boolComparison(bool lhs, QNode* rhs)
{
  if ((lhs == true) && (rhs->type == QNodeTrueValue))
    return true;

  if ((lhs == false) && (rhs->type == QNodeFalseValue))
    return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// stringComparison
//
static bool stringComparison(KjNode* lhsP, QNode* rhs, bool isTimestamp)
{
  if (rhs->type != QNodeStringValue)
    return false;

  if (isTimestamp)  // Then LHS is a Float?
  {
    char   errorString[256];
    double rhsTimestamp = dateTimeFromString(rhs->value.s, errorString, sizeof(errorString));

    return (lhsP->value.f == rhsTimestamp);
  }

  if (rhs->type != QNodeStringValue)
    return false;

  return (strcmp(lhsP->value.s, rhs->value.s) == 0);
}



// -----------------------------------------------------------------------------
//
// qCommalistCompare - lexical analysis of an ngsi-ld Q-filter
//
bool qCommaListCompare(KjNode* lhsNode, QNode* rhs, bool isTimestamp)
{
  if (isTimestamp == true)  // The first in the list id a FLOAT, the rest need to be converted to float
  {
    double timestamp;
    char   errorString[256];

    if (lhsNode->type == KjFloat)
      timestamp = lhsNode->value.f;
    else if (lhsNode->type == KjString)
      timestamp = dateTimeFromString(lhsNode->value.s, errorString, sizeof(errorString));
    else
      return false;

    QNode* child1 = rhs->value.children;

    if (child1->type != QNodeFloatValue)
      LM_E(("CLIST: Internal Error (LHS is a timestamp but the first in the RHS list is not a FLOAT ..."));
    else if (child1->value.f == timestamp)
      return true;

    // Compare the rest of the children in the comma list (converting them to FLOAT)
    for (QNode* rhP = child1->next; rhP != NULL; rhP = rhP->next)
    {
      if (rhP->type == QNodeFloatValue)
      {
        if (rhP->value.f == timestamp)
          return true;
      }
      else if (rhP->type == QNodeStringValue)
      {
        char   errorString[256];
        double rhsTimestamp = dateTimeFromString(rhP->value.s, errorString, sizeof(errorString));

        if (rhsTimestamp == timestamp)
          return true;
      }
    }

    return false;
  }

  for (QNode* rhP = rhs->value.children; rhP != NULL; rhP = rhP->next)
  {
    switch (lhsNode->type)
    {
    case KjInt:     if (intComparison(lhsNode->value.i,    rhP)              == true) return true; break;
    case KjFloat:   if (floatComparison(lhsNode,           rhP, isTimestamp) == true) return true; break;  // isTimestamp - then LHS is turned into a Float ... Right?
    case KjString:  if (stringComparison(lhsNode,          rhP, isTimestamp) == true) return true; break;  //               Can't be both - need to test empirically
    case KjBoolean: if (boolComparison(lhsNode->value.b,   rhP)              == true) return true; break;
    default:
      break;
    }
  }

  return false;
}

