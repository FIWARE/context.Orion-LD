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
* Author: Carsten Frey
*/
#include <stdio.h>                                               // snprintf
#include <string.h>                                              // strlen, strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
}

#include "orionld/types/QNode.h"                                 // QNode, QNodeType
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/urlDecode.h"                            // urlDecode
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/q/qLex.h"                                      // qLex
#include "orionld/q/qParse.h"                                    // qParse
#include "orionld/troe/qTreeToSql.h"                             // Own interface



// -----------------------------------------------------------------------------
//
// sqlBufAppend - append a string to the SQL buffer, growing if needed
//
static void sqlBufAppend(char** bufP, int* posP, int* sizeP, const char* str)
{
  int len = strlen(str);

  while (*posP + len + 1 > *sizeP)
  {
    int   newSize = *sizeP * 2;
    char* newBuf  = (char*) kaAlloc(&orionldState.kalloc, newSize);

    memcpy(newBuf, *bufP, *posP);
    *bufP  = newBuf;
    *sizeP = newSize;
  }

  memcpy(*bufP + *posP, str, len);
  *posP += len;
  (*bufP)[*posP] = 0;
}



// -----------------------------------------------------------------------------
//
// sqlBufAppendEscaped - append a string value with SQL single-quote escaping
//
static void sqlBufAppendEscaped(char** bufP, int* posP, int* sizeP, const char* str)
{
  sqlBufAppend(bufP, posP, sizeP, "'");

  for (const char* p = str; *p != 0; p++)
  {
    if (*p == '\'')
      sqlBufAppend(bufP, posP, sizeP, "''");
    else
    {
      char c[2] = { *p, 0 };
      sqlBufAppend(bufP, posP, sizeP, c);
    }
  }

  sqlBufAppend(bufP, posP, sizeP, "'");
}



// -----------------------------------------------------------------------------
//
// valueColumn - determine which column to use based on the value node type
//
static const char* valueColumn(QNodeType type)
{
  switch (type)
  {
  case QNodeIntegerValue:  return "number";
  case QNodeFloatValue:    return "number";
  case QNodeStringValue:   return "text";
  case QNodeTrueValue:     return "boolean";
  case QNodeFalseValue:    return "boolean";
  default:                 return "text";
  }
}



// -----------------------------------------------------------------------------
//
// sqlOperator - get SQL operator string for a QNode comparison type
//
static const char* sqlOperator(QNodeType type)
{
  switch (type)
  {
  case QNodeEQ:       return "=";
  case QNodeNE:       return "!=";
  case QNodeGT:       return ">";
  case QNodeGE:       return ">=";
  case QNodeLT:       return "<";
  case QNodeLE:       return "<=";
  case QNodeMatch:    return "~";
  case QNodeNoMatch:  return "!~";
  default:            return "=";
  }
}



// -----------------------------------------------------------------------------
//
// appendValue - append a value node as SQL literal
//
static void appendValue(QNode* nodeP, char** bufP, int* posP, int* sizeP)
{
  char numBuf[64];

  switch (nodeP->type)
  {
  case QNodeIntegerValue:
    snprintf(numBuf, sizeof(numBuf), "%lld", nodeP->value.i);
    sqlBufAppend(bufP, posP, sizeP, numBuf);
    break;

  case QNodeFloatValue:
    snprintf(numBuf, sizeof(numBuf), "%f", nodeP->value.f);
    sqlBufAppend(bufP, posP, sizeP, numBuf);
    break;

  case QNodeStringValue:
    sqlBufAppendEscaped(bufP, posP, sizeP, nodeP->value.s);
    break;

  case QNodeTrueValue:
    sqlBufAppend(bufP, posP, sizeP, "true");
    break;

  case QNodeFalseValue:
    sqlBufAppend(bufP, posP, sizeP, "false");
    break;

  case QNodeRegexpValue:
    sqlBufAppendEscaped(bufP, posP, sizeP, nodeP->value.re);
    break;

  default:
    sqlBufAppend(bufP, posP, sizeP, "NULL");
    break;
  }
}



// -----------------------------------------------------------------------------
//
// expandVariable - expand short attribute name to full URI
//
// The qTree was parsed with qToDbModel=false, so variable names are not expanded.
// We expand them here for PostgreSQL (the attributes table stores expanded URIs).
//
static const char* expandVariable(QNode* varNodeP)
{
  return orionldAttributeExpand(orionldState.contextP, varNodeP->value.v, true, NULL);
}



// -----------------------------------------------------------------------------
//
// qComparisonToSql - convert a comparison node to an EXISTS subquery
//
// Input tree structure for e.g. QNodeGT:
//   QNodeGT -> children: [QNodeVariable "P1", QNodeIntegerValue 5]
//
// Output SQL:
//   EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id
//           AND id = 'https://..../P1' AND number > 5)
//
static void qComparisonToSql(QNode* compP, char** bufP, int* posP, int* sizeP)
{
  QNode* lhsP = compP->value.children;   // Left-hand side (variable)
  QNode* rhsP = (lhsP != NULL) ? lhsP->next : NULL;  // Right-hand side (value)

  if (lhsP == NULL || lhsP->type != QNodeVariable)
  {
    KT_E("qComparisonToSql: unexpected LHS node type");
    return;
  }

  const char* expandedAttr = expandVariable(lhsP);

  if (rhsP == NULL)
  {
    KT_E("qComparisonToSql: missing RHS");
    return;
  }

  // Handle Range: attr==lower..upper → BETWEEN
  if (rhsP->type == QNodeRange)
  {
    QNode*      lowerP = rhsP->value.children;
    QNode*      upperP = (lowerP != NULL) ? lowerP->next : NULL;
    const char* col    = (lowerP != NULL) ? valueColumn(lowerP->type) : "number";

    sqlBufAppend(bufP, posP, sizeP, "EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id AND id = '");
    sqlBufAppend(bufP, posP, sizeP, expandedAttr);
    sqlBufAppend(bufP, posP, sizeP, "' AND ");
    sqlBufAppend(bufP, posP, sizeP, col);
    sqlBufAppend(bufP, posP, sizeP, " >= ");
    if (lowerP != NULL) appendValue(lowerP, bufP, posP, sizeP);
    sqlBufAppend(bufP, posP, sizeP, " AND ");
    sqlBufAppend(bufP, posP, sizeP, col);
    sqlBufAppend(bufP, posP, sizeP, " <= ");
    if (upperP != NULL) appendValue(upperP, bufP, posP, sizeP);
    sqlBufAppend(bufP, posP, sizeP, ")");
    return;
  }

  // Handle Comma list: attr==val1,val2 → IN (val1,val2)
  if (rhsP->type == QNodeComma)
  {
    QNode*      firstVal = rhsP->value.children;
    const char* col      = (firstVal != NULL) ? valueColumn(firstVal->type) : "text";

    sqlBufAppend(bufP, posP, sizeP, "EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id AND id = '");
    sqlBufAppend(bufP, posP, sizeP, expandedAttr);
    sqlBufAppend(bufP, posP, sizeP, "' AND ");
    sqlBufAppend(bufP, posP, sizeP, col);

    if (compP->type == QNodeNE)
      sqlBufAppend(bufP, posP, sizeP, " NOT IN (");
    else
      sqlBufAppend(bufP, posP, sizeP, " IN (");

    bool first = true;
    for (QNode* valP = firstVal; valP != NULL; valP = valP->next)
    {
      if (!first)
        sqlBufAppend(bufP, posP, sizeP, ", ");
      appendValue(valP, bufP, posP, sizeP);
      first = false;
    }

    sqlBufAppend(bufP, posP, sizeP, "))");
    return;
  }

  // Standard comparison: attr op value
  const char* col   = valueColumn(rhsP->type);
  const char* sqlOp = sqlOperator(compP->type);

  sqlBufAppend(bufP, posP, sizeP, "EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id AND id = '");
  sqlBufAppend(bufP, posP, sizeP, expandedAttr);
  sqlBufAppend(bufP, posP, sizeP, "' AND ");
  sqlBufAppend(bufP, posP, sizeP, col);
  sqlBufAppend(bufP, posP, sizeP, " ");
  sqlBufAppend(bufP, posP, sizeP, sqlOp);
  sqlBufAppend(bufP, posP, sizeP, " ");
  appendValue(rhsP, bufP, posP, sizeP);
  sqlBufAppend(bufP, posP, sizeP, ")");
}



// -----------------------------------------------------------------------------
//
// qExistsToSql - convert an Exists/NotExists node to SQL
//
// QNodeExists -> children: [QNodeVariable "P1"]
//   → EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id AND id = '...P1')
//
// QNodeNotExists -> children: [QNodeVariable "P1"]
//   → NOT EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id AND id = '...P1')
//
static void qExistsToSql(QNode* existsP, char** bufP, int* posP, int* sizeP)
{
  QNode* varP = existsP->value.children;

  if (varP == NULL || varP->type != QNodeVariable)
  {
    KT_E("qExistsToSql: expected variable child");
    return;
  }

  const char* expandedAttr = expandVariable(varP);

  if (existsP->type == QNodeNotExists)
    sqlBufAppend(bufP, posP, sizeP, "NOT ");

  sqlBufAppend(bufP, posP, sizeP, "EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id AND id = '");
  sqlBufAppend(bufP, posP, sizeP, expandedAttr);
  sqlBufAppend(bufP, posP, sizeP, "')");
}



// forward declaration
static void qNodeToSql(QNode* nodeP, char** bufP, int* posP, int* sizeP);



// -----------------------------------------------------------------------------
//
// qLogicalToSql - convert AND/OR node to SQL with proper grouping
//
static void qLogicalToSql(QNode* logicalP, char** bufP, int* posP, int* sizeP)
{
  const char* op = (logicalP->type == QNodeAnd) ? " AND " : " OR ";

  sqlBufAppend(bufP, posP, sizeP, "(");

  bool first = true;
  for (QNode* childP = logicalP->value.children; childP != NULL; childP = childP->next)
  {
    if (!first)
      sqlBufAppend(bufP, posP, sizeP, op);

    qNodeToSql(childP, bufP, posP, sizeP);
    first = false;
  }

  sqlBufAppend(bufP, posP, sizeP, ")");
}



// -----------------------------------------------------------------------------
//
// qNodeToSql - recursive dispatch for any QNode type
//
static void qNodeToSql(QNode* nodeP, char** bufP, int* posP, int* sizeP)
{
  switch (nodeP->type)
  {
  case QNodeAnd:
  case QNodeOr:
    qLogicalToSql(nodeP, bufP, posP, sizeP);
    break;

  case QNodeEQ:
  case QNodeNE:
  case QNodeGT:
  case QNodeGE:
  case QNodeLT:
  case QNodeLE:
  case QNodeMatch:
  case QNodeNoMatch:
    qComparisonToSql(nodeP, bufP, posP, sizeP);
    break;

  case QNodeExists:
  case QNodeNotExists:
    qExistsToSql(nodeP, bufP, posP, sizeP);
    break;

  default:
    KT_E("qNodeToSql: unsupported node type %d", nodeP->type);
    break;
  }
}



// -----------------------------------------------------------------------------
//
// qTreeToSql - convert a QNode tree to a SQL WHERE clause fragment
//
// The returned string is a SQL condition fragment suitable for appending
// to a WHERE clause with " AND ".
//
// Example: for q=P1>5;P2=="hello", returns:
//   "(EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id
//     AND id = 'https://.../P1' AND number > 5)
//    AND EXISTS (SELECT 1 FROM attributes WHERE entityid = entities.id
//     AND id = 'https://.../P2' AND text = 'hello'))"
//
// Returns NULL on error or if qTree is NULL.
//
const char* qTreeToSql(QNode* qTree)
{
  if (qTree == NULL)
    return NULL;

  int   bufSize = 2048;
  char* buf     = (char*) kaAlloc(&orionldState.kalloc, bufSize);
  int   pos     = 0;

  buf[0] = 0;

  qNodeToSql(qTree, &buf, &pos, &bufSize);

  if (pos == 0)
    return NULL;

  return buf;
}



// -----------------------------------------------------------------------------
//
// troeQStringToSql - parse a q-string and convert to SQL WHERE clause fragment
//
// This function parses the q-string WITHOUT the MongoDB model transformations
// (no dotForEq, no attrs. prefix, no .value suffix). Variable names are left
// as short names and expanded in qTreeToSql via orionldAttributeExpand().
//
// Returns the SQL WHERE clause fragment, or NULL on error (orionldError set).
//
const char* troeQStringToSql(char* qString)
{
  if (qString == NULL || qString[0] == 0)
    return NULL;

  char*   title;
  char*   detail;

  urlDecode(qString);

  QNode* lexList = qLex(qString, true, &title, &detail);
  if (lexList == NULL)
  {
    orionldError(OrionldBadRequestData, title, detail, 400);
    return NULL;
  }

  // Parse WITHOUT DB model transformation (forDb=false, qToDbModel=false)
  // This keeps variable names as short names without MongoDB-specific mangling
  QNode* qTree = qParse(lexList, NULL, false, false, &title, &detail);
  if (qTree == NULL)
  {
    orionldError(OrionldBadRequestData, title, detail, 400);
    return NULL;
  }

  return qTreeToSql(qTree);
}
