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
#include <string.h>                                            // strncpy

extern "C"
{
#include "kbase/kStringSplit.h"                                // kStringSplit
#include "kalloc/kaStrdup.h"                                   // kaStrdup
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "orionld/types/QNode.h"                               // QNode
#include "orionld/types/OrionLdRestService.h"                  // OrionLdRestService
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/dotForEq.h"                           // dotForEq
#include "orionld/context/orionldAttributeExpand.h"            // orionldAttributeExpand
#include "orionld/context/orionldSubAttributeExpand.h"         // orionldSubAttributeExpand
#include "orionld/serviceRoutines/orionldPostSubscriptions.h"  // orionldPostSubscriptions
#include "orionld/serviceRoutines/orionldPatchSubscription.h"  // orionldPatchSubscription
#include "orionld/q/qVariableFix.h"                            // Own interface



// ----------------------------------------------------------------------------
//
// qVariableFix -
//
// A
// A.B
// A[B]
// A.B[C]
// A.B[C.D]
// createdAt|modifiedAt                                      (entity timestamps)
// A.(createdAt|modifiedAt|unitCode|observedAt\ngsildproof)  (attribute timestamps or special sub-attrs)
// A.B.(createdAt|modifiedAt)                                (sub-attribute timestamps)
// A.B.C.(createdAt|modifiedAt)                              (sub-sub-attribute timestamps)
//
char* qVariableFix(char* varPathIn, bool forDb, bool* isMdP, char** detailsP)
{
  // Entity timestamp?
  if      (strcmp(varPathIn, "createdAt")  == 0)    return (char*) "creDate";
  else if (strcmp(varPathIn, "modifiedAt") == 0)    return (char*) "modDate";

  char   qVar[1024];
  strncpy(qVar, varPathIn, sizeof(qVar) - 1);
  char*  attrPath     = qVar;
  char*  valuePath    = NULL;
  bool   isMd         = false;

  //
  // Brackets?
  //
  char* startBracket = strchr(qVar, '[');

  if (startBracket != NULL)
  {
    char* endBracket = strchr(qVar, ']');
    *startBracket = 0;

    if (endBracket == NULL)
    {
      *detailsP = (char*) "missing end bracket";
      LM_W(("Bad Input (%s)", *detailsP));
      return NULL;
    }

    if (endBracket < startBracket)
    {
      *detailsP = (char*) "syntax error in 'q' (ending bracket before initial bracket)";
      LM_W(("Bad Input (%s)", *detailsP));
      return NULL;
    }

    *endBracket = 0;
    valuePath   = &startBracket[1];
  }

  LM_T(LmtQ, ("*************************************************************************************"));
  LM_T(LmtQ, ("Q-Attribute Path: '%s'", attrPath));
  LM_T(LmtQ, ("Q-Value Path: '%s'", valuePath));

  //
  // Split attrPath into array with dot as delimiter
  // After that, expand each of the items
  // Then, dot-to-eq
  // Lastly, render the items as one string, with dots as "separator"
  //
  char* attrArray[10];
  int   items    = kStringSplit(attrPath, '.', attrArray, 10);
  bool  addValue = forDb;

  LM_T(LmtQ2, ("Attr Path Items: %d", items));
  if ((items == 2) && ((strcmp(attrArray[1], "createdAt") == 0) || (strcmp(attrArray[1], "modifiedAt") == 0)))
    isMd = false;
  else if (items > 1)
    isMd   = true;

  *isMdP = isMd;
  LM_T(LmtQ2, ("isMd: %s", (isMd == true)? "true" : "false"));

  // Special case: createdAt, modifiedAt, observedAt
  // Entities, Attributes and sub-attributes have creDate/modDate in the DB
  // Deeper levels use createdAt/modifiedAy
  //
  bool isCreatedAt  = (strcmp(attrArray[items - 1], "createdAt")  == 0);
  bool isModifiedAt = (strcmp(attrArray[items - 1], "modifiedAt") == 0);
  bool isObservedAt = (strcmp(attrArray[items - 1], "observedAt") == 0);
  bool isTimestamp  = isCreatedAt || isModifiedAt || isObservedAt;

  if ((forDb == true) && (isCreatedAt || isModifiedAt))
  {
    addValue = false;

    if (isCreatedAt)
      attrArray[items - 1] = (char*) "creDate";
    else if (isModifiedAt)
      attrArray[items - 1] = (char*) "modDate";
  }
  bool inSubscription = (orionldState.serviceP->serviceRoutine == orionldPostSubscriptions) || (orionldState.serviceP->serviceRoutine == orionldPatchSubscription);
  if (inSubscription)
  {
    if (isObservedAt == true)
      addValue = false;
    else
      addValue = true;
  }

  for (int ix = 0; ix < items; ix++)
  {
    LM_T(LmtQ2, ("Attr Item %d: '%s'", ix, attrArray[ix]));
    if (ix == 0)
      attrArray[ix] = orionldAttributeExpand(orionldState.contextP, attrArray[ix], true, NULL);
    else if ((isTimestamp == true) && (ix == (items - 1)))
    {}  // Not expanding timestamps
    else
      attrArray[ix] = orionldSubAttributeExpand(orionldState.contextP, attrArray[ix], true, NULL);

    attrArray[ix] = kaStrdup(&orionldState.kalloc, attrArray[ix]);
    dotForEq(attrArray[ix]);
    LM_T(LmtQ, ("Q-Attribute %d: '%s'", ix, attrArray[ix]));
  }

  // Calculate the buffer size needed for the rendered string
  int pathLen = 15;  // strlen("attrs.") + strlen("md.") + strlen("value")

  for (int ix = 0; ix < items; ix++)
  {
    pathLen += strlen(attrArray[ix]) + 1;
  }

  if (valuePath != NULL)
    pathLen += 6 + strlen(valuePath) + 1;  // strlen(".value") + zero delimiter

  char* path = kaAlloc(&orionldState.kalloc, pathLen);

  int last = 0;
  for (int ix = 0; ix < items; ix++)
  {
    int bytes = 0;

    if (forDb == true)
    {
      if (ix == 0)
      {
        snprintf(&path[last], pathLen - last, "attrs");
        last += 5;
      }
      else if ((isMd == true) && (ix == 1))
      {
        LM_T(LmtQ2, ("Adding '.md' to the path"));
        snprintf(&path[last], pathLen - last, ".md");
        last += 3;
      }
    }

    if ((ix == 0) && (forDb == false))
      bytes = snprintf(&path[last], pathLen - last, "%s", attrArray[ix]);
    else
      bytes = snprintf(&path[last], pathLen - last, ".%s", attrArray[ix]);

    last += bytes;
  }

  if (addValue == true)
  {
    int bytes = snprintf(&path[last], pathLen - last, ".value");
    last += bytes;
  }

  if (valuePath != NULL)
    snprintf(&path[last], pathLen - last, ".%s", valuePath);

  LM_T(LmtQ, ("Final path: '%s'", path));
  LM_T(LmtQ, ("*************************************************************************************"));
  LM_T(LmtQ3, ("NEW Returning '%s' (forDb: '%s')", path, (forDb == true)? "true" : "false"));
  return path;
}
