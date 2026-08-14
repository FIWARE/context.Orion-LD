/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/types/DistOp.h"                                // DistOp
#include "orionld/types/RegistrationMode.h"                      // RegistrationMode
#include "orionld/types/DistOpType.h"                            // DistOpType
#include "orionld/types/StringArray.h"                           // StringArray
#include "orionld/types/RegCache.h"                              // RegCache
#include "orionld/types/RegCacheItem.h"                          // RegCacheItem
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/regMatch/regMatchOperation.h"                  // regMatchOperation
#include "orionld/regMatch/regMatchInformationArrayForQuery.h"   // regMatchInformationArrayForQuery
#include "orionld/distOp/viaMatch.h"                             // viaMatch
#include "orionld/distOp/distOpListsMerge.h"                     // distOpListsMerge
#include "orionld/regCache/regCacheSem.h"                        // regCacheSemTake, regCacheSemGive
#include "orionld/regMatch/regMatchForEntitiesQuery.h"           // Own interface



// -----------------------------------------------------------------------------
//
// regMatchForEntitiesquery -
//
DistOp* regMatchForEntitiesQuery
(
  RegistrationMode  regMode,
  DistOpType        opType,
  StringArray*      idListP,
  StringArray*      typeListP,
  StringArray*      attrListP
)
{
  DistOp* distOpList = NULL;

  //
  // The walk runs under the READ lock: the list must not change under us, and the items the
  // matching DistOps are built from must stay alive long enough to be pinned (distOpCreate).
  //
  regCacheSemTake(orionldState.tenantP->regCache, __FUNCTION__, "Matching registrations", SemReadOp);
  for (RegCacheItem* regP = orionldState.tenantP->regCache->regList; regP != NULL; regP = regP->next)
  {
    if ((regP->mode & regMode) == 0)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to RegistrationMode ('%s' vs '%s')", regP->regId, registrationModeToString(regP->mode), registrationModeToString(regMode));
      continue;
    }

    if (regMatchOperation(regP, opType) == false)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to Operation (operation == '%s')", regP->regId, distOpTypeToString(opType));
      continue;
    }

    // Loop detection
    if (viaMatch(orionldState.in.via, regP->hostAlias) == true)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to Loop (Via)", regP->regId);
      continue;
    }

    DistOp* distOpP = regMatchInformationArrayForQuery(regP, idListP, typeListP, attrListP);
    if (distOpP == NULL)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to Information Array", regP->regId);
      continue;
    }

    //
    // Add distOpP to the linked list (distOpList)
    //
    KT_T(KtRegMatch, "%s: Reg Match !", regP->regId);

    distOpList = distOpListsMerge(distOpList, distOpP);
  }
  regCacheSemGive(orionldState.tenantP->regCache, __FUNCTION__, "Matching registrations");

  return distOpList;
}
