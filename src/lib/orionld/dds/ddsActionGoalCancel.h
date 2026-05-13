#ifndef SRC_LIB_ORIONLD_DDS_DDSACTIONGOALCANCEL_H_
#define SRC_LIB_ORIONLD_DDS_DDSACTIONGOALCANCEL_H_

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
// ddsActionGoalCancelIfMapped -
//
// If 'attrShortName' is mapped to a DDS action, parse 'datasetId' as a
// "urn:goal:<uuid>" identifier and ask the enabler to cancel the matching
// in-flight goal. No-op if attribute is not mapped, datasetId is NULL, or the
// UUID fails to parse. Drops the matching DdsActionGoal from the local list.
//
// Returns true if a cancel was issued, false otherwise.
//
extern bool ddsActionGoalCancelIfMapped(const char* attrShortName, const char* datasetId);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONGOALCANCEL_H_
