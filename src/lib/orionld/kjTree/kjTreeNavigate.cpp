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
#include <string.h>                                              // strncpy
#include <unistd.h>                                              // NULL

extern "C"
{
#include "ktrace/kTrace.h"                                       // KTrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjNavigate.h"                                    // kjNavigate2
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // Trace levels for kTrace
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/common/pathComponentsSplit.h"                  // pathComponentsSplit
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/kjTree/kjTreeLog.h"                            // KT_TREE



// -----------------------------------------------------------------------------
//
// dotCount - count number of dots in a string (no of components in a path)
//
static int dotCount(char* s)
{
  int dots = 0;

  while (*s != 0)
  {
    if (*s == '.')
      ++dots;
    ++s;
  }

  return dots;
}



// -----------------------------------------------------------------------------
//
// kjTreeNavigate - prepared for db-model, but also OK without
//
KjNode* kjTreeNavigate(KjNode* treeP, const char* pathIn, bool* isTimestampP)
{
  char path[512];
  strncpy(path, pathIn, sizeof(path) - 1);

  KT_T(KtCsf, "Looking for '%s'", path);
  KT_TREE(treeP, "In this tree", KtCsf);

  int components = dotCount(path) + 1;
  if (components > 20)
    KT_X(1, "The current implementation of Orion-LD can only handle 20 levels of tree navigation");

  char* compV[20];

  // pathComponentsSplit destroys the path, but 'path' is already a local copy
  components = pathComponentsSplit(path, compV);

  //
  // - the first component is always the longName of the ATTRIBUTE
  // - the second is either "value", "object", "languageMap", or the longName of the SUB-ATTRIBUTE
  //
  // 'attrs' and 'md' don't exist in an API entity and must be nulled out here.
  //

  //
  // Is it a timestamp?   (if so, an ISO8601 string must be turned into a float/integer to be compared
  //
  if (isTimestampP != NULL)
  {
    char* lastComponent = compV[components - 1];
    if ((strcmp(lastComponent, "observedAt") == 0) || (strcmp(lastComponent, "modifiedAt") == 0) || (strcmp(lastComponent, "createdAt") == 0))
      *isTimestampP = true;
    else
      *isTimestampP = false;
  }

  compV[components] = NULL;

  KjNode* result = kjNavigate2(treeP, compV);
  if (result != NULL)
    return result;

  //
  // Nothing found
  //
  //   What if it's due to '.' vs '=' ...
  //   Yes, I know, this is messy, some order is needed
  //
  // FIXME: Fix this!
  //
  eqForDot(compV[0]);  // As it IS an Attribute
  if (components > 1)
    eqForDot(compV[1]);  // As it MIGHT be a Sub-Attribute (and if not, it has no '=')

  result = kjNavigate2(treeP, compV);
  if (result != NULL)
    return result;

  //
  // Could be a Relationship ...
  // Perhaps I should "bake in" the value|object|languageMap inside kjNavigate ...
  //
  if ((components == 2) && (strcmp(compV[1], "value") == 0))
  {
    compV[1] = (char*) "object";
    result = kjNavigate2(treeP, compV);
    if (result != NULL)
      return result;

    // FIXME: Here I put the '=' back ... Even messier now :(
    dotForEq(compV[0]);
    result = kjNavigate2(treeP, compV);
    if (result != NULL)
      return result;
  }

  return NULL;
}
