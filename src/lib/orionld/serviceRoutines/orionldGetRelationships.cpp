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
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/mongoc/mongocRelationshipsGet.h"                  // mongocRelationshipsGet
#include "orionld/serviceRoutines/orionldGetRelationships.h"        // Own Interface

#include "orionld/common/traceLevels.h"                          // KTrace levels



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
  KT_T(StLinked, "Getting Entity '%s'", orionldState.wildcard[0]);

  if ((experimental == true) && (orionldState.in.legacy == NULL))
    referencedEntitiesP = mongocRelationshipsGet(orionldState.wildcard[0]);
  else
  {
    // output error and return
    KT_E("GetRelationships dos only support the new MongoDB C++ driver (start orionld with -experimental)");
    orionldError(OrionldInternalError, "Endpoint not supported", "GetRelationships dos only support the new MongoDB C++ driver (start orionld with -experimental)", 500);
    return false;
  }
 
  
  orionldState.responseTree = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(orionldState.responseTree, kjString(orionldState.kjsonP, "id", orionldState.wildcard[0]));

  KjNode* idNodeP  = kjArray(orionldState.kjsonP, "referencedBy"); 
  kjChildAdd(orionldState.responseTree, idNodeP);

  idNodeP->value.firstChildP = referencedEntitiesP->value.firstChildP;  
  
  orionldState.httpStatusCode = 200;
  return true;
}
