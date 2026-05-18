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
// Internal DDS cleanup: pull a per-goal datasetId instance from an
// action-tied attribute's @datasets array (direct mongo write, so no
// orionldDeleteAttribute - which means the DDS goal-cancel hook in
// that routine is naturally skipped, as it should be for a goal that
// has already terminated). Subscription dispatch fires via
// ddsActionLifecycleNotify so subscribers see the instance disappear.
//
// TODO: TRoE sees this lifecycle as a series of attribute updates
// followed by no record of the disappearance (pull doesn't emit a TRoE
// entry today). Revisit when temporal sub-attribute patches are
// prioritised.
//
extern void ddsActionInstanceDelete
(
  const char* entityId,
  const char* entityType,
  const char* attributeName,
  const char* datasetIdStr
);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONINSTANCEDELETE_H_
