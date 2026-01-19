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
extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd, ...
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjStringValueLookupInArray.h"                    // kjStringValueLookupInArray
}

#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails
#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/types/RegCache.h"                              // RegCache
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/mongoc/mongocRelationshipsGet.h"               // mongocRelationshipsGet
#include "orionld/serviceRoutines/orionldGetRelationships.h"     // Own Interface





// ----------------------------------------------------------------------------
//
// orionldGetRelationships -
//
bool orionldGetRelationships(void)
{
  KjNode*                referencedEntitiesP;

  //
  // If the broker is started with '-experimental', then mongocEntityTypeGet is to be used instead of mongoCppEntityTypeGet.
  // - Except if the HTTP Header 'legacy' is used
  //
  if (orionldState.uriParams.id == NULL)
  {
    orionldError(OrionldBadRequestData, "No entity id specified in URL", "No entity id specified in URL", 400);
    return false;
  }

  KT_T(StLinked, "Getting Entity '%s'", orionldState.uriParams.id);

  // pipelines are only supported with the new MongoDB C++ driver so serve the endpoint only when experimental mode is on and legacy mode is not requested
  if ((experimental == true) && (orionldState.in.legacy == NULL))
    referencedEntitiesP = mongocRelationshipsGet(orionldState.uriParams.id);
  else
  {
    // output error and return
    orionldError(OrionldInternalError, "Endpoint not supported", "GetRelationships does only support the new MongoDB C++ driver (start orionld with -experimental)", 500);
    return false;
  }
 
  
  orionldState.responseTree = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(orionldState.responseTree, kjString(orionldState.kjsonP, "id", orionldState.uriParams.id));

  //KjNode* idNodeP  = kjArray(orionldState.kjsonP, "referencedBy"); 
  kjChildAdd(orionldState.responseTree, referencedEntitiesP);

  //idNodeP->value.firstChildP = referencedEntitiesP;  
  
  orionldState.httpStatusCode = 200;
  return true;
}
