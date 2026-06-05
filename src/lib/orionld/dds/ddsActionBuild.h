#ifndef SRC_LIB_ORIONLD_DDS_DDSACTIONBUILD_H_
#define SRC_LIB_ORIONLD_DDS_DDSACTIONBUILD_H_

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
#include <stdint.h>                                              // int64_t

#include "ddsenabler_participants/rpc/RpcTypes.hpp"              // UUID, StatusCode

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}



// -----------------------------------------------------------------------------
//
// Helpers for building the NGSI-LD sub-attribute Property tree that wraps a
// DDS action callback (feedback / result / status). Same envelope idea as
// ddsReplyBuild.cpp uses for services. The service reply carries a requestId
// because it lives on the default instance; an action envelope needs no such
// id — its goal is identified by the per-goal datasetId of the instance it
// sits on.
//
// All constructed nodes live in orionldState.kjsonP — callers must have
// orionldState initialized for the current thread.
//

// -----------------------------------------------------------------------------
//
// ddsActionUuidToString -
//
// Render the 16-byte UUID into canonical 8-4-4-4-12 hex form
// ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"). The output buffer must be at
// least 37 bytes (36 chars + NUL). Returns the buffer for convenient inlining.
//
extern char* ddsActionUuidToString(const eprosima::ddsenabler::participants::UUID& uuid, char* out);


// -----------------------------------------------------------------------------
//
// ddsActionGoalDatasetId -
//
// Build the datasetId URI for a goal: "urn:goal:<uuid>". The output buffer
// must be at least 48 bytes ("urn:goal:" = 9 + 36 UUID chars + NUL = 46).
//
extern char* ddsActionGoalDatasetId(const eprosima::ddsenabler::participants::UUID& uuid, char* out);


// -----------------------------------------------------------------------------
//
// ddsActionDatasetIdToUuid -
//
// Parse "urn:goal:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (or just the 36-char
// UUID portion) into a 16-byte UUID. Returns true on success, false if the
// input doesn't match the canonical form.
//
extern bool ddsActionDatasetIdToUuid(const char* datasetId, eprosima::ddsenabler::participants::UUID& out);


// -----------------------------------------------------------------------------
//
// ddsActionStatusCodeToString -
//
// Lowercased status string ("unknown" / "accepted" / "executing" / "canceling" /
// "succeeded" / "canceled" / "aborted" / "rejected" / "timeout" / "failed" /
// "cancelRequestFailed"). Matches the ROS2 action-status vocabulary in spirit;
// lowercased so it reads as a value, not an enum identifier.
//
extern const char* ddsActionStatusCodeToString(eprosima::ddsenabler::participants::StatusCode code);


// -----------------------------------------------------------------------------
//
// ddsActionBuildSubAttribute -
//
// Build a sub-attribute Property of the form
//
//   "<subName>": {
//     "type": "Property",
//     "value": <payloadValue>,
//     "publishedAt":      { "type": "Property", "value": <secs since epoch> }
//     [+ optional: instanceHandleId / participantId / ddsDataType ]
//   }
//
// The goal is identified by the enclosing instance's datasetId
// ("urn:goal:<uuid>"), so no goalId sub-Property is stamped on the envelope.
//
// 'payloadValue' is renamed in-place to "value" — pass a node the caller
// doesn't need elsewhere.
//
extern KjNode* ddsActionBuildSubAttribute
(
  const char*                                       subName,
  KjNode*                                           payloadValue,
  const char*                                       instanceHandleId,
  const char*                                       participantId,
  const char*                                       ddsDataType,
  int64_t                                           publishedAt
);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONBUILD_H_
