#ifndef SRC_LIB_ORIONLD_DDS_DDSREPLYBUILD_H_
#define SRC_LIB_ORIONLD_DDS_DDSREPLYBUILD_H_

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
#include <stdint.h>                                              // uint64_t, int64_t

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}



// -----------------------------------------------------------------------------
//
// Helpers for building the NGSI-LD request/reply sub-attribute Property tree
// that wraps a DDS service call. Used by both the async reply path
// (ddsServiceReplyNotification.cpp) and the ddsSync request path
// (ddsSyncPatchEntity.cpp).
//
// All constructed nodes live in orionldState.kjsonP - callers must have
// orionldState initialized for the current thread.
//

// Build a Property whose value is a string: { "<name>": { type:Property, value:"<value>" } }
extern KjNode* ddsReplyStringPropertyNode(const char* name, const char* value);

// Build a Property whose value is an integer: { "<name>": { type:Property, value:<value> } }
extern KjNode* ddsReplyIntegerPropertyNode(const char* name, long long value);

// Tease out participantId / ddsDataType / xId from the enabler's reply envelope
// and return the actual reply payload (a child of 'replyTree' - caller must
// clone it before reusing the tree for anything else).
extern KjNode* ddsReplyExtractMetadata
(
  KjNode*       replyTree,
  const char**  participantIdP,
  const char**  ddsDataTypeP,
  const char**  xIdP
);

// Build a "request" or "reply" sub-attribute Property. 'payloadValue' becomes
// the Property value (renamed in-place to "value") - pass a node the caller
// doesn't need elsewhere.
extern KjNode* ddsReplyBuildSubAttribute
(
  const char*  subName,
  KjNode*      payloadValue,
  uint64_t     requestId,
  const char*  xId,
  const char*  participantId,
  const char*  ddsDataType,
  int64_t      publishedAt
);

#endif  // SRC_LIB_ORIONLD_DDS_DDSREPLYBUILD_H_
