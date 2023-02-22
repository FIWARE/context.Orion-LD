/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include "kjson/kjRender.h"                                    // kjFastRender
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/troe/PgTableDefinitions.h"                   // PG_ATTRIBUTE_INSERT_START, PG_SUB_ATTRIBUTE_INSERT_START
#include "orionld/troe/PgAppendBuffer.h"                       // PgAppendBuffer
#include "orionld/troe/pgAppendInit.h"                         // pgAppendInit
#include "orionld/troe/pgAppend.h"                             // pgAppend
#include "orionld/troe/pgAttributeBuild.h"                     // pgAttributeBuild
#include "orionld/troe/pgCommands.h"                           // pgCommands
#include "orionld/troe/troePutAttribute.h"                   // Own interface



// ----------------------------------------------------------------------------
//
// troePutAttribute -
//
// Input payload body is simply an attribute.
// The attribute's value must be pushed onto the 'attributes' table, with an opMode set to "REPLACE".
// Updating the type of an attribute is not allowed - 'type' if present, is removed by the Service Routine.
//
bool troePutAttribute(void)
{
  char*           entityId       = orionldState.wildcard[0];
  PgAppendBuffer  attributes;
  PgAppendBuffer  subAttributes;

  pgAppendInit(&attributes, 1024);  // Just a single attribute - 1024 should be enough
  pgAppendInit(&subAttributes, 4 * 1024);

  pgAppend(&attributes,    PG_ATTRIBUTE_INSERT_START,     0);  // pgAppend does strlen of PG_ATTRIBUTE_INSERT_START - for now!
  pgAppend(&subAttributes, PG_SUB_ATTRIBUTE_INSERT_START, 0);  // pgAppend does strlen of PG_SUB_ATTRIBUTE_INSERT_START - for now!

  pgAttributeBuild(&attributes, "Replace", entityId, orionldState.requestTree, &subAttributes);

  char* sqlV[2];
  int   sqlIx = 0;

  if (attributes.values    > 0) sqlV[sqlIx++] = attributes.buf;
  if (subAttributes.values > 0) sqlV[sqlIx++] = subAttributes.buf;

  if (sqlIx > 0)
    pgCommands(sqlV, sqlIx);

  return true;
}
