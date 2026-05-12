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
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd, kjString
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/serviceRoutines/orionldPatchEntity2.h"         // orionldPatchEntity2
#include "orionld/service/serviceLookupByServiceRoutine.h"       // serviceLookupByServiceRoutine
#include "orionld/dds/ddsActionBuild.h"                          // ddsActionBuildSubAttribute, ddsActionGoalDatasetId
#include "orionld/dds/ddsActionSubAttributeUpdate.h"             // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionSubAttributeUpdate -
//
// Build a merge-patch tree of the form:
//
//   { "<attrLongName>": {
//       "type": "Property",
//       "datasetId": "urn:goal:<uuid>",
//       "<subAttributeName>": { ... envelope ... }
//   } }
//
// and call orionldPatchEntity2.
//
void ddsActionSubAttributeUpdate
(
  const char*                                       entityId,
  const char*                                       entityType,
  const char*                                       attributeName,
  const char*                                       subAttributeName,
  KjNode*                                           subAttributeValue,
  const eprosima::ddsenabler::participants::UUID&   goalId,
  const char*                                       instanceHandleId,
  const char*                                       participantId,
  const char*                                       ddsDataType,
  int64_t                                           publishTime
)
{
  orionldStateInit(NULL);

  char* attrLongName = orionldAttributeExpand(orionldState.contextP, attributeName, true, NULL);

  //
  // Envelope sub-attribute (goalId/publishedAt/etc. inside)
  //
  KjNode* envelope = ddsActionBuildSubAttribute(subAttributeName,
                                                subAttributeValue,
                                                goalId,
                                                instanceHandleId,
                                                participantId,
                                                ddsDataType,
                                                publishTime);

  //
  // datasetId for the action attribute instance — keyed by goalId
  //
  char datasetIdStr[48];
  ddsActionGoalDatasetId(goalId, datasetIdStr);

  KjNode* entityBody     = kjObject(orionldState.kjsonP, NULL);
  KjNode* attrBody       = kjObject(orionldState.kjsonP, attrLongName);
  KjNode* attrTypeNode   = kjString(orionldState.kjsonP, "type", "Property");
  KjNode* datasetIdNode  = kjString(orionldState.kjsonP, "datasetId", datasetIdStr);

  kjChildAdd(attrBody, attrTypeNode);
  kjChildAdd(attrBody, datasetIdNode);
  kjChildAdd(attrBody, envelope);
  kjChildAdd(entityBody, attrBody);

  char* expandedType = orionldContextItemExpand(orionldState.contextP, entityType, true, NULL);

  orionldState.requestTree         = entityBody;
  orionldState.wildcard[0]         = (char*) entityId;
  orionldState.tenantP             = &tenant0;
  orionldState.ddsSample           = true;
  orionldState.ddsPublishTime      = publishTime;
  orionldState.apiVersion          = API_VERSION_NGSILD_V1;
  orionldState.uriParams.type      = expandedType;
  orionldState.serviceP            = serviceLookupByServiceRoutine(orionldPatchEntity2, HTTP_PATCH);

  KT_T(StDdsAction, "Merge-patching entity '%s' with '%s' for attribute '%s' (datasetId %s)",
       entityId, subAttributeName, attributeName, datasetIdStr);
  orionldPatchEntity2();

  void* con_cls;
  extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);
  requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);
}
