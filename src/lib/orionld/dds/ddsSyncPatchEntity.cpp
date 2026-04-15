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
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // trace messages
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjBuilder.h"                                     // kjChildAdd
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/DdsService.h"                            // DdsService, DdsServiceInstance
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // StDdsService
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/dds/ddsService.h"                              // ddsService
#include "orionld/dds/ddsServiceLookupByAttributeName.h"         // ddsServiceLookupByAttributeName
#include "orionld/dds/ddsInstance.h"                             // ddsInstanceFree
#include "orionld/dds/ddsReplyBuild.h"                           // ddsReplyBuildSubAttribute
#include "orionld/dds/ddsSyncPatchEntity.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// patchAttrSync - if 'attrP' maps to a configured DDS service, send the
// request synchronously and merge request/reply sub-attributes into 'attrP'.
// If it doesn't map to a DDS service, passes through unchanged (returns true).
//
static bool patchAttrSync(KjNode* attrP, bool* processedP)
{
  if ((attrP == NULL) || (attrP->type != KjObject) || (attrP->name == NULL))
    return true;

  // Skip NGSI-LD housekeeping fields that sneak in at the attribute layer.
  if ((strcmp(attrP->name, "id") == 0) || (strcmp(attrP->name, "type") == 0))
    return true;

  const char* attrShortName = orionldContextItemAliasLookup(orionldState.contextP, attrP->name, NULL, NULL);
  DdsService* serviceP      = ddsServiceLookupByAttributeName(attrShortName);

  if (serviceP == NULL)
    return true;  // Not a DDS-tied attr - passthrough

  KjNode* valueP = kjLookup(attrP, "value");
  if ((valueP == NULL) || (valueP->type != KjObject))
  {
    // Scalar / missing value - nothing meaningful to ship to a DDS service.
    // Matches the async path (ddsPublishAttribute) which just skips with a
    // warning rather than rejecting the PATCH.
    KT_W("ddsSync: attribute '%s' maps to DDS service '%s' but value is not an object - skipping DDS call",
         attrShortName, serviceP->name);
    return true;
  }

  KT_T(StDdsService, "ddsSync: sending request for attribute '%s' (service '%s')",
       attrShortName, serviceP->name);

  DdsServiceInstance* dsiP = NULL;
  if (!ddsService(serviceP, valueP, true, &dsiP))
    return false;  // orionldError already set (503 or 504)

  //
  // Build the request sub-attribute. Clone dsiP->requestTree (lives in the
  // instance's kalloc) into orionldState.kjsonP so it survives after
  // ddsInstanceFree below.
  //
  KjNode* requestValue = kjClone(orionldState.kjsonP, dsiP->requestTree);
  KjNode* requestSub   = ddsReplyBuildSubAttribute("request",
                                                   requestValue,
                                                   dsiP->requestId,
                                                   NULL,   // xId           - not exposed by enabler on request side
                                                   NULL,   // participantId - not exposed by enabler on request side
                                                   serviceP->requestType,
                                                   dsiP->publishedAt);
  kjChildAdd(attrP, requestSub);

  //
  // Build the reply sub-attribute from the instance's reply fields.
  //
  if (dsiP->replyTree != NULL)
  {
    KjNode* replyValue = kjClone(orionldState.kjsonP, dsiP->replyTree);
    KjNode* replySub   = ddsReplyBuildSubAttribute("reply",
                                                   replyValue,
                                                   dsiP->requestId,
                                                   dsiP->xId,
                                                   dsiP->participantId,
                                                   dsiP->ddsDataType,
                                                   dsiP->replyPublishedAt);
    kjChildAdd(attrP, replySub);
  }

  ddsInstanceFree(dsiP);
  *processedP = true;
  return true;
}



// -----------------------------------------------------------------------------
//
// ddsSyncPatchEntityProcess -
//
bool ddsSyncPatchEntityProcess(KjNode* requestTree, bool* anyProcessedP)
{
  *anyProcessedP = false;

  if ((requestTree == NULL) || (requestTree->type != KjObject))
    return true;

  for (KjNode* attrP = requestTree->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (!patchAttrSync(attrP, anyProcessedP))
      return false;
  }

  return true;
}
