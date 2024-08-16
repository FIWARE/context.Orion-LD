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
#include <unistd.h>                                              // NULL

extern "C"
{
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/common/pathComponentsSplit.h"                  // pathComponentsSplit
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/kjTree/kjNavigate.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// kjNavigate -
//
// FIXME: move to kjson library
//
KjNode* kjNavigate(KjNode* treeP, const char** pathCompV, KjNode** parentPP, bool* onlyLastMissingP)
{
  KjNode* hitP = kjLookup(treeP, pathCompV[0]);

  if (parentPP != NULL)
    *parentPP = treeP;

  if (hitP == NULL)  // No hit - we're done
  {
    if (onlyLastMissingP != NULL)
      *onlyLastMissingP = (pathCompV[1] == NULL)? true : false;

    return NULL;
  }

  if (pathCompV[1] == NULL)  // Found it - we're done
    return hitP;

  return kjNavigate(hitP, &pathCompV[1], parentPP, onlyLastMissingP);  // Recursive call, one level down
}



// -----------------------------------------------------------------------------
//
// kjNavigate - true Kj-Tree navigation
//
KjNode* kjNavigate(KjNode* treeP, char** compV)
{
  KjNode* hitP = kjLookup(treeP, compV[0]);

  if (hitP == NULL)
    return NULL;

  if (compV[1] == NULL)
    return hitP;

  return kjNavigate(hitP, &compV[1]);
}



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
// kjNavigate2 - prepared for db-model, but also OK without
//
KjNode* kjNavigate2(KjNode* treeP, char* path, bool* isTimestampP)
{
  LM_T(LmtCsf, ("Looking for '%s'", path));
  kjTreeLog(treeP, "In this tree", LmtCsf);

  int components = dotCount(path) + 1;
  if (components > 20)
    LM_X(1, ("The current implementation of Orion-LD can only handle 20 levels of tree navigation"));

  char* compV[20];

  // pathComponentsSplit destroys the path, I need to work on a copy
  char* pathCopy = kaStrdup(&orionldState.kalloc, path);
  components = pathComponentsSplit(pathCopy, compV);

  //
  // - the first component is always the longName of the ATTRIBUTE
  // - the second is either "value", "object", "languageMap", or the longName of the SUB-ATTRIBUTE
  //
  // 'attrs' and 'md' don't exist in an API entity and must be nulled out here.
  //

  //
  // Is it a timestamp?   (if so, an ISO8601 string must be turned into a float/integer to be compared
  //
  LM_T(LmtCsf, ("Here; components: %d", components));
  char* lastComponent = compV[components - 1];
  if ((strcmp(lastComponent, "observedAt") == 0) || (strcmp(lastComponent, "modifiedAt") == 0) || (strcmp(lastComponent, "createdAt") == 0))
    *isTimestampP = true;
  else
    *isTimestampP = false;

  compV[components] = NULL;

  LM_T(LmtCsf, ("Here"));
  KjNode* result = kjNavigate(treeP, compV);
  if (result != NULL)
    return result;
  LM_T(LmtCsf, ("Here"));

  //
  // Nothing found
  //
  //   What if it's due to '.' vs '=' ...
  //   Yes, I know, this is messy, some order is needed
  //
  // FIXME: Fix this!
  //
  LM_T(LmtCsf, ("Here"));
  eqForDot(compV[0]);  // As it IS an Attribute
  LM_T(LmtCsf, ("Here"));
  if (components > 1)
    eqForDot(compV[1]);  // As it MIGHT be a Sub-Attribute (and if not, it has no '=')
  LM_T(LmtCsf, ("Here"));

  result = kjNavigate(treeP, compV);
  if (result != NULL)
    return result;

  //
  // Could be a Relationship ...
  // Perhaps I should "bake in" the value|object|languageMap inside kjNavigate ...
  //
  if ((components == 2) && (strcmp(compV[1], "value") == 0))
  {
    compV[1] = (char*) "object";
    result = kjNavigate(treeP, compV);
    if (result != NULL)
      return result;

    // FIXME: Here I put the '=' back ... Even messier now :(
    dotForEq(compV[0]);
    result = kjNavigate(treeP, compV);
    if (result != NULL)
      return result;
  }

  return NULL;
}



