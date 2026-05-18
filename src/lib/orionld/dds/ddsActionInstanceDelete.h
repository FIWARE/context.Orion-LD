#ifndef SRC_LIB_ORIONLD_DDS_DDSACTIONINSTANCEDELETE_H_
#define SRC_LIB_ORIONLD_DDS_DDSACTIONINSTANCEDELETE_H_

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



// -----------------------------------------------------------------------------
//
// ddsActionInstanceDelete -
//
// Internal DDS cleanup: delete a per-goal datasetId instance of an
// action-tied attribute. Sets orionldState.noNotify=true so subscription
// dispatch is skipped AND the ddsActionGoalCancelIfMapped hook inside
// orionldDeleteAttribute is bypassed (the goal has already terminated -
// there's nothing to cancel). TRoE still records the deletion.
//
// TODO: TRoE sees this lifecycle as create+delete of the per-goal instance,
// which is correct but not ideal - semantically it's a Modify (final state
// stamped onto an existing instance). Revisit when temporal sub-attribute
// patches are prioritised.
//
extern void ddsActionInstanceDelete
(
  const char* entityId,
  const char* entityType,
  const char* attributeName,
  const char* datasetIdStr
);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONINSTANCEDELETE_H_
