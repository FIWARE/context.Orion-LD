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
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/RegCache.h"                              // RegCache
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocRegistrationsIter.h"              // mongocRegistrationsIter
#include "orionld/dbModel/dbModelToApiRegistration.h"            // dbModelToApiRegistration
#include "orionld/regCache/regCacheItemAdd.h"                    // regCacheItemAdd
#include "orionld/regCache/regCacheItemContextCheck.h"           // regCacheItemContextCheck
#include "orionld/regCache/regCacheItemFromDb.h"                 // regCacheItemFromDbTree
#include "orionld/regCache/regCacheSem.h"                        // regCacheSemInit
#include "orionld/regCache/regCacheCreate.h"                     // Own interface



extern void apiModelToCacheRegistration(KjNode* apiRegistrationP);
// -----------------------------------------------------------------------------
//
// regIterFunc -
//
int regIterFunc(RegCache* rcP, KjNode* dbRegP)
{
  //
  // The body lives in regCacheItemFromDbTree - the HA sync needs to do exactly
  // this to one registration when another instance creates or changes it, and two
  // copies of it would drift.
  //
  regCacheItemFromDbTree(rcP, dbRegP);

  return 0;
}



// -----------------------------------------------------------------------------
//
// regCacheCreate -
//
RegCache* regCacheCreate(OrionldTenant* tenantP, bool scanRegs)
{
  RegCache* rcP = (RegCache*) malloc(sizeof(RegCache));

  if (rcP == NULL)
    KT_RE(NULL, "Out of memory (attempt to create a registration cache)");

  rcP->tenantP  = tenantP;
  rcP->regList  = NULL;
  rcP->last     = NULL;

  regCacheSemInit(rcP);  // BEFORE the scan below - regCacheItemAdd takes the lock

  if (scanRegs)
  {
    if (mongocRegistrationsIter(rcP, regIterFunc) != 0)
      KT_E("mongocRegistrationsIter failed");
  }

  return rcP;
}
