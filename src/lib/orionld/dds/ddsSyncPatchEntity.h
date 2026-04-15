#ifndef SRC_LIB_ORIONLD_DDS_DDSSYNCPATCHENTITY_H_
#define SRC_LIB_ORIONLD_DDS_DDSSYNCPATCHENTITY_H_

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
// ddsSyncPatchEntityProcess - walk an NGSI-LD entity tree and, for every
// attribute whose short name matches a configured DDS service, send the
// service request synchronously (blocking up to ddsSyncTimeoutMs for the
// reply). On reply, merge 'request' and 'reply' sub-attribute Properties
// into that attribute so the downstream mongo write / notifications / TRoE
// see the final state.
//
// Returns true on success (including: no DDS-tied attrs in tree, or tree
// is NULL). Returns false if any DDS roundtrip failed (send failure -> 503,
// or timeout -> 504) - orionldError has already been set. The caller MUST
// then skip the mongo write to guarantee atomic rollback across all attrs
// in the payload.
//
extern bool ddsSyncPatchEntityProcess(KjNode* requestTree);

#endif  // SRC_LIB_ORIONLD_DDS_DDSSYNCPATCHENTITY_H_
