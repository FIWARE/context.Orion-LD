/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/RegCache.h"                              // RegCache
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocRegistrationGet.h"                // mongocRegistrationGet
#include "orionld/dbModel/dbModelToApiRegistration.h"            // dbModelToApiRegistration
#include "orionld/regCache/regCacheItemAdd.h"                    // regCacheItemAdd
#include "orionld/regCache/regCacheItemRemove.h"                 // regCacheItemRemove
#include "orionld/regCache/regCacheItemContextCheck.h"           // regCacheItemContextCheck
#include "orionld/regCache/regCacheItemFromDb.h"                 // Own interface

extern void apiModelToCacheRegistration(KjNode* apiRegistrationP);



// -----------------------------------------------------------------------------
//
// regCacheItemFromDbTree -
//
bool regCacheItemFromDbTree(RegCache* rcP, KjNode* dbRegP)
{
  // Convert DB Reg to API Reg - in place
  if (dbModelToApiRegistration(dbRegP, true, true) == false)
    KT_RE(false, "dbModelToApiRegistration failed");

  KjNode* apiRegP = dbRegP;

  // If an @context is given for the registration, make sure it's valid
  OrionldContext* fwdContextP = NULL;

  if (regCacheItemContextCheck(apiRegP, NULL, &fwdContextP) == false)
    KT_RE(false, "Unable to resolve a Registration @context for a reg-cache item");

  KjNode* regIdNodeP = kjLookup(apiRegP, "id");
  char*   regId      = (regIdNodeP != NULL)? regIdNodeP->value.s : (char*) "no:reg:id";

  // Convert API Reg to Cache Reg
  apiModelToCacheRegistration(apiRegP);

  regCacheItemAdd(rcP, regId, apiRegP, true, fwdContextP);

  return true;
}



// -----------------------------------------------------------------------------
//
// regCacheItemFromDb -
//
bool regCacheItemFromDb(OrionldTenant* tenantP, const char* registrationId)
{
  if (tenantP->regCache == NULL)
    KT_RE(false, "No registration cache for tenant '%s' - registration '%s' not cached", tenantP->tenant, registrationId);

  //
  // Re-running the conversion reports through orionldError, and this can be
  // called with no request behind it at all - so whatever it has to say must not
  // land in somebody else's HTTP response. Same guard, and for the same reason,
  // as subCacheItemFromDb.
  //
  int                    savedStatusCode = orionldState.httpStatusCode;
  OrionldProblemDetails  savedPd         = orionldState.pd;

  KjNode* dbRegP = mongocRegistrationGet(registrationId);

  if (dbRegP == NULL)
  {
    orionldState.httpStatusCode = savedStatusCode;
    orionldState.pd             = savedPd;
    KT_RE(false, "Registration '%s' not found in the database - not cached", registrationId);
  }

  //
  // Out with the old copy first. The reg cache has no update-in-place, and a
  // second item with the same id would be matched twice by every forwarded
  // request.
  //
  regCacheItemRemove(tenantP->regCache, registrationId);

  bool ok = regCacheItemFromDbTree(tenantP->regCache, dbRegP);

  orionldState.httpStatusCode = savedStatusCode;
  orionldState.pd             = savedPd;

  KT_T(KtRegCache, "Registration '%s' %s the reg cache from the database", registrationId, ok? "added to" : "NOT added to");

  return ok;
}
