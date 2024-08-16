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
#include <unistd.h>                                            // NULL

extern "C"
{
#include "kalloc/kaStrdup.h"                                   // kaStrdup
#include "kjson/KjNode.h"                                      // KjNode
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/dotForEq.h"                           // dotForEq
#include "orionld/types/QNode.h"                               // QNode
#include "orionld/context/orionldContextItemExpand.h"          // orionldContextItemExpand
#include "orionld/kjTree/kjNavigate.h"                         // kjNavigate2
#include "orionld/q/qRangeCompare.h"                           // qRangeCompare
#include "orionld/q/qMatchCompare.h"                           // qMatchCompare
#include "orionld/q/qCommaListCompare.h"                       // qCommaListCompare
#include "orionld/q/qEqCompare.h"                              // qEqCompare
#include "orionld/q/qGtCompare.h"                              // qGtCompare
#include "orionld/q/qLtCompare.h"                              // qLtCompare



// -----------------------------------------------------------------------------
//
// qMatch -
//
bool qMatch(QNode* qP, KjNode* attributesP, bool eqNames)
{
  if (qP->type == QNodeOr)
  {
    // If any of the children is a match, then it's a match
    int childNo = 0;
    for (QNode* childP = qP->value.children; childP != NULL; childP = childP->next)
    {
      if (qMatch(childP, attributesP, eqNames) == true)
        return true;
      ++childNo;
    }
  }
  else if (qP->type == QNodeAnd)
  {
    // If ALL of the children are a match, then it's a match
    for (QNode* childP = qP->value.children; childP != NULL; childP = childP->next)
    {
      if (qMatch(childP, attributesP, eqNames) == false)
        return false;
    }

    return true;
  }
  else
  {
    QNode* lhs      = qP->value.children;        // variable-path
    QNode* rhs      = qP->value.children->next;  // constant (NULL for QNodeExists & QNodeNotExists)
    char*  longName = NULL;

    if (eqNames == true)
    {
      longName = orionldContextItemExpand(orionldState.contextP, lhs->value.v, true, NULL);
      longName = kaStrdup(&orionldState.kalloc, longName);
      dotForEq(longName);
    }
    else
      longName = lhs->value.v;

    //
    // Not OR nor AND => LHS is an  attribute from the entity
    //
    // Well, or a sub-attribute, or a fragment of its value ...
    // Anyway, the attribute/sub-attribute must exist.
    // If it does not, then the result is always "false" (except for the case "q=!P1", of course :))
    //
    bool     isTimestamp = false;
    KjNode*  lhsNode     = kjNavigate2(attributesP, longName, &isTimestamp);

    //
    // If Left-Hand-Side does not exist - MATCH for op "NotExist" and No Match for all other operations
    //
    if (lhsNode == NULL)
      return (qP->type == QNodeNotExists)? true : false;

    if      (qP->type == QNodeNotExists)  return false;
    else if (qP->type == QNodeExists)     return true;
    else if (qP->type == QNodeEQ)
    {
      if      (rhs->type == QNodeRange)   return  qRangeCompare(lhsNode, rhs, isTimestamp);
      else if (rhs->type == QNodeComma)   return  qCommaListCompare(lhsNode, rhs, isTimestamp);
      else                                return  qEqCompare(lhsNode, rhs, isTimestamp);
    }
    else if (qP->type == QNodeNE)
    {
      if      (rhs->type == QNodeRange)   return !qRangeCompare(lhsNode, rhs, isTimestamp);
      else if (rhs->type == QNodeComma)   return !qCommaListCompare(lhsNode, rhs, isTimestamp);
      else                                return !qEqCompare(lhsNode, rhs, isTimestamp);
    }
    else if (qP->type == QNodeGT)         return  qGtCompare(lhsNode, rhs, isTimestamp);
    else if (qP->type == QNodeLT)         return  qLtCompare(lhsNode, rhs, isTimestamp);
    else if (qP->type == QNodeGE)         return !qLtCompare(lhsNode, rhs, isTimestamp);
    else if (qP->type == QNodeLE)         return !qGtCompare(lhsNode, rhs, isTimestamp);
    else if (qP->type == QNodeMatch)      return qMatchCompare(lhsNode, rhs);
    else if (qP->type == QNodeNoMatch)    return !qMatchCompare(lhsNode, rhs);
    else if (qP->type == QNodeComma)      return false;
    else if (qP->type == QNodeRange)      return false;
  }

  return false;
}

