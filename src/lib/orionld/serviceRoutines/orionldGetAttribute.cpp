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
extern "C"
{
#include "kbase/kMacros.h"                                       // K_FT
#include "ktrace/kTrace.h"                                       // KT_T, ...
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjObject
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KTrace trace levels
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/dds/kjTreeLog.h"                               // kjTreeLog2
#include "orionld/dbModel/dbModelToApiAttribute.h"               // dbModelToApiAttribute
#include "orionld/apiModel/ntonAttribute.h"                      // ntonAttribute
#include "orionld/apiModel/ntocAttribute.h"                      // ntocAttribute
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup



// -----------------------------------------------------------------------------
//
// orionldGetAttribute -
//
bool orionldGetAttribute(void)
{
  char* entityId     = orionldState.wildcard[0];
  char* entityType   = NULL;
  char* attrLongName = orionldState.in.pathAttrExpanded;

  //
  // If the entity type is set as URI param (only ONE), then it will be used, but first it needs to be expanded.
  // In the case the entity "entityId" exists, but with another type, it is not considered a hit - 404
  //
  if (orionldState.in.typeList.items == 1)
    entityType = orionldState.in.typeList.array[0];
  else if (orionldState.in.typeList.items != 0)
  {
    orionldError(OrionldBadRequestData, "Invalid URI param (type)", "more than one entity type", 400);
    return false;
  }

  //
  // Prepare the array of ONE single attribute name (the one from the URL PATH, expanded if needed)
  //
  char*       array[2] = { attrLongName, NULL };
  StringArray attrList = { 1, array };

  // attrList.items    = 1;
  // attrList.array    = array;
  // attrList.array[0] =   attrLongName;

  KT_T(StSR, "attrLongName: '%s'", attrLongName);


  //
  // Get the Attribute from the database (together with Entity ID and TYPE)
  //
  KjNode* dbEntityP = mongocEntityLookup(entityId, entityType, &attrList, orionldState.uriParams.geometryProperty, NULL);
  if (dbEntityP == NULL)
  {
    orionldError(OrionldResourceNotFound, "Combination Entity/Attribute Not Found", attrLongName, 404);
    return false;
  }

  //
  // GET the Attribute from inside the DB Entity
  //
  KjNode* dbAttrsP = kjLookup(dbEntityP, "attrs");
  if (dbAttrsP == NULL)
  {
    orionldError(OrionldInternalError, "Database Error (entity without 'attrs' field)", NULL, 500);
    return false;
  }

  char* attrNameEq   = kaStrdup(&orionldState.kalloc, orionldState.in.pathAttrExpanded);
  dotForEq(attrNameEq);

  KjNode* dbAttrP = kjLookup(dbAttrsP, attrNameEq);
  if (dbAttrP == NULL)
  {
    orionldError(OrionldResourceNotFound, "Combination Entity/Attribute Not Found", attrLongName, 404);
    return false;
  }

  OrionldProblemDetails pd;
  orionldState.responseTree   = dbModelToApiAttribute2(dbAttrP, NULL, orionldState.uriParamOptions.sysAttrs, orionldState.out.format, orionldState.uriParams.lang, false, &pd);
  orionldState.httpStatusCode = 200;

  return true;
}
