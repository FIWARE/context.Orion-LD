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
#include "ktrace/kTrace.h"                                       // trace messages
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // StDdsAction
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/context/orionldAttributeExpand.h"              // orionldAttributeExpand
#include "orionld/mongoc/mongocDatasetInstanceOps.h"              // mongocDatasetInstancePull
#include "orionld/dds/ddsActionInstanceDelete.h"                 // Own interface



void ddsActionInstanceDelete
(
  const char* entityId,
  const char* entityType,
  const char* attributeName,
  const char* datasetIdStr
)
{
  (void) entityType;  // unused in the direct-mongo path

  if (orionldState.tenantP == NULL)
    orionldState.tenantP = &tenant0;

  char* attrLongName = orionldAttributeExpand(orionldState.contextP, attributeName, true, NULL);

  KT_T(StDdsAction, "Pulling per-goal instance from @datasets: entity '%s' attr '%s' datasetId '%s'",
       entityId, attributeName, datasetIdStr);

  (void) mongocDatasetInstancePull(entityId, attrLongName, datasetIdStr);
}
