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

#include "orionld/types/RegistrationMode.h"                      // registrationMode
#include "orionld/types/RegCache.h"                              // RegCache
#include "orionld/types/RegCacheItem.h"                          // RegCacheItem
#include "orionld/types/DistOp.h"                                // DistOp
#include "orionld/types/DistOpType.h"                            // DistOpType
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/distOp/xForwardedForMatch.h"                   // xForwardedForMatch
#include "orionld/distOp/viaMatch.h"                             // viaMatch
#include "orionld/regMatch/regMatchOperation.h"                  // regMatchOperation
#include "orionld/regMatch/regMatchInformationArray.h"           // regMatchInformationArray
#include "orionld/regCache/regCacheSem.h"                        // regCacheSemTake, regCacheSemGive
#include "orionld/regMatch/regMatchForEntityCreation.h"          // Own interface


#if 0
// -----------------------------------------------------------------------------
//
// distOpListDebug -
//
void distOpListDebug(DistOp* distOpP, const char* what)
{
  KT_T(KtDistOpList, "----- DistOp List: %s", what);

  while (distOpP != NULL)
  {
    KT_T(KtDistOpList, "  Registration:      %s", distOpP->regP->regId);
    KT_T(KtDistOpList, "  Operation:         %s", distOpTypes[distOpP->operation]);

    if (distOpP->error == true)
    {
      KT_T(KtDistOpList, "  Title:             %s", distOpP->title);
      KT_T(KtDistOpList, "  Detail:            %s", distOpP->detail);
      KT_T(KtDistOpList, "  Status:            %d", distOpP->httpResponseCode);
    }

    if (distOpP->requestBody != NULL)
    {
      KT_T(KtDistOpList, "  Attributes:");
      int ix = 0;
      for (KjNode* attrP = distOpP->requestBody->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        if ((strcmp(attrP->name, "id") != 0) && (strcmp(attrP->name, "type") != 0))
        {
          KT_T(KtDistOpList, "    Attribute %d:   '%s'", ix, attrP->name);
          ++ix;
        }
      }
    }

    if (distOpP->attrList != NULL)
    {
      KT_T(KtDistOpList, "  URL Attributes:        %d", distOpP->attrList->items);
      for (int ix = 0; ix < distOpP->attrList->items; ix++)
      {
        KT_T(KtDistOpList, "    Attribute %d:   '%s'", ix, distOpP->attrList->array[ix]);
      }
    }

    distOpP = distOpP->next;
  }

  KT_T(KtDistOpList, "---------------------");
}
#endif



// -----------------------------------------------------------------------------
//
// regMatchForEntityCreation -
//
// To match for Entity Creation, a registration needs:
// - "mode" != "auxiliary"
// - "operations" must include "createEntity
// - "information" must match by entity id+type and attributes if present in the registration
//
DistOp* regMatchForEntityCreation
(
  RegistrationMode regMode,     // Exclusive, Redirect, Inclusive, Auxiliar
  DistOpType       operation,   // createEntity, patchAttribute, ...
  const char*      entityId,
  const char*      entityType,
  KjNode*          payloadBody
)
{
  DistOp* distOpHead = NULL;
  DistOp* distOpTail = NULL;

  KT_T(KtRegMatch, "Registration Mode: %d (%s)", regMode, registrationModeToString(regMode));
  KT_T(KtRegMatch, "Operation:         %d (%s)", operation, distOpTypes[operation]);
  KT_T(KtRegMatch, "Entity ID:         %s", entityId);
  KT_T(KtRegMatch, "Entity Type:       %s", entityType);

  //
  // The walk runs under the READ lock: the list must not change under us, and the items the
  // matching DistOps are built from must stay alive long enough to be pinned (distOpCreate).
  //
  regCacheSemTake(orionldState.tenantP->regCache, __FUNCTION__, "Matching registrations", SemReadOp);
  for (RegCacheItem* regP = orionldState.tenantP->regCache->regList; regP != NULL; regP = regP->next)
  {
    if ((regP->mode & regMode) == 0)
    {
      // KT_T(KtRegMatch, "%s: No Reg Match due to regMode (0x%x vs 0x%x)", regP->regId, regP->mode, regMode);
      continue;
    }

    // Loop detection
    if (viaMatch(orionldState.in.via, regP->hostAlias) == true)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to Loop (Via)", regP->regId);
      continue;
    }

    if (xForwardedForMatch(orionldState.in.xForwardedFor, regP->ipAndPort) == true)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to loop detection", regP->regId);
      continue;
    }

    if ((regMode != RegModeExclusive) && (regMatchOperation(regP, operation) == false))
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to Operation (operation == %d: '%s')", regP->regId, operation, distOpTypes[operation]);
      continue;
    }

    DistOp* distOpP = regMatchInformationArray(regP, operation, entityId, entityType, payloadBody);
    if (distOpP == NULL)
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to Information Array", regP->regId);
      continue;
    }

    KT_T(KtRegMatch, "%s: Match!", regP->regId);

    //
    // If Exclusive, we now need to check the Operation (DistOpType)
    // If not a match, the distOpP needs to be marked as ERROR (409)
    //
    if ((regMode == RegModeExclusive) && (regMatchOperation(regP, operation) == false))
    {
      KT_T(KtRegMatch, "%s: No Reg Match due to 'matching exclusive registration forbids the Operation' (operation == %d: '%s')", regP->regId, operation, distOpTypes[operation]);
      for (DistOp* doP = distOpP; doP != NULL; doP = doP->next)
      {
        doP->error            = true;
        doP->errorType        = OrionldAlreadyExists;
        // doP->errorSubType     = registration-not-supporting-operation
        doP->title            = (char*) "Operation not supported";
        doP->detail           = (char*) "A matching exclusive registration forbids the Operation";
        doP->httpResponseCode = 409;
        // doP->entityId   = entityId;
        // doP->entityType = entityType;
        // doP->registrationId = regP->registrationId;
      }
    }

    // Add extra info in DistOp, needed by forwardRequestSend
    distOpP->operation = operation;

    // Add distOpP to the linked list
    if (distOpHead == NULL)
      distOpHead = distOpP;
    else
      distOpTail->next = distOpP;

    distOpTail       = distOpP;
    distOpTail->next = NULL;
  }
  regCacheSemGive(orionldState.tenantP->regCache, __FUNCTION__, "Matching registrations");

  return distOpHead;
}
