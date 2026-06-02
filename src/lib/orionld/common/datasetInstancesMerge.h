#ifndef SRC_LIB_ORIONLD_COMMON_DATASETINSTANCESMERGE_H_
#define SRC_LIB_ORIONLD_COMMON_DATASETINSTANCESMERGE_H_

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
#include "kjson/KjNode.h"                                        // KjNode
}



// -----------------------------------------------------------------------------
//
// datasetInstancesMerge - NGSI-LD per-instance merge of a PATCH's dataset instances onto the DB's
//
// Returns a NEW array (allocated in orionldState.kjsonP, unnamed) that is the
// per-instance merge, for ONE attribute, of patchArrayP (the patch's datasetId
// instances) onto dbArrayP (the attribute's datasetId instances currently in the
// DB - may be NULL or non-array, meaning "nothing in the DB yet"):
//
//   - DB instance whose datasetId IS in the patch  -> cloned, then the patch
//     instance's fields are overlaid on top (so the instance is updated, while
//     sub-attributes the patch does not mention survive)
//   - DB instance whose datasetId is NOT in the patch -> cloned, kept verbatim
//   - patch instance whose datasetId is NOT in the DB -> appended (truly new)
//
// DB instances keep their original order; new instances are appended after them.
// Inputs are not modified.
//
extern KjNode* datasetInstancesMerge(KjNode* dbArrayP, KjNode* patchArrayP);

#endif  // SRC_LIB_ORIONLD_COMMON_DATASETINSTANCESMERGE_H_
