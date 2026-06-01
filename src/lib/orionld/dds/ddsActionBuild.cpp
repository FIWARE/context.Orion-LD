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
#include <stdint.h>                                              // int64_t, uint8_t
#include <stdio.h>                                               // snprintf
#include <string.h>                                              // strncmp

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjChildAdd
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/dds/ddsReplyBuild.h"                           // ddsReplyStringPropertyNode, ddsReplyIntegerPropertyNode
#include "orionld/dds/ddsActionBuild.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// ddsActionUuidToString -
//
char* ddsActionUuidToString(const eprosima::ddsenabler::participants::UUID& uuid, char* out)
{
  // Canonical RFC 4122 5-group form: 8-4-4-4-12 hex chars, hyphens between groups.
  // Byte ordering follows the "big-endian textual" convention used by ROS2 /
  // Fast-DDS: bytes[0..15] map to the 16 octets in left-to-right reading order.
  snprintf(out, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           uuid[0],  uuid[1],  uuid[2],  uuid[3],
           uuid[4],  uuid[5],
           uuid[6],  uuid[7],
           uuid[8],  uuid[9],
           uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
  return out;
}



// -----------------------------------------------------------------------------
//
// ddsActionGoalDatasetId -
//
char* ddsActionGoalDatasetId(const eprosima::ddsenabler::participants::UUID& uuid, char* out)
{
  // "urn:goal:" + 36 UUID chars + NUL = 46
  char uuidStr[37];
  ddsActionUuidToString(uuid, uuidStr);
  snprintf(out, 48, "urn:goal:%s", uuidStr);
  return out;
}



// -----------------------------------------------------------------------------
//
// hexNibble - return -1 on non-hex input
//
static int hexNibble(char c)
{
  if ((c >= '0') && (c <= '9'))  return c - '0';
  if ((c >= 'a') && (c <= 'f'))  return c - 'a' + 10;
  if ((c >= 'A') && (c <= 'F'))  return c - 'A' + 10;
  return -1;
}



// -----------------------------------------------------------------------------
//
// ddsActionDatasetIdToUuid -
//
bool ddsActionDatasetIdToUuid(const char* datasetId, eprosima::ddsenabler::participants::UUID& out)
{
  if (datasetId == NULL)
    return false;

  // Optional "urn:goal:" prefix.
  const char* p = datasetId;
  if (strncmp(p, "urn:goal:", 9) == 0)
    p += 9;

  // Canonical form: 8-4-4-4-12 hex with hyphens at positions 8, 13, 18, 23.
  static const int hyphenPositions[] = { 8, 13, 18, 23 };
  int hyphenIx = 0;
  int byteIx   = 0;
  int hiNib    = -1;

  for (int i = 0; i < 36; ++i)
  {
    char c = p[i];
    if ((hyphenIx < 4) && (i == hyphenPositions[hyphenIx]))
    {
      if (c != '-') return false;
      ++hyphenIx;
      continue;
    }

    int n = hexNibble(c);
    if (n < 0) return false;

    if (hiNib < 0)
      hiNib = n;
    else
    {
      out[byteIx++] = (uint8_t) ((hiNib << 4) | n);
      hiNib = -1;
    }
  }

  return (byteIx == 16) && (p[36] == 0);
}



// -----------------------------------------------------------------------------
//
// ddsActionStatusCodeToString -
//
const char* ddsActionStatusCodeToString(eprosima::ddsenabler::participants::StatusCode code)
{
  switch (code)
  {
  case eprosima::ddsenabler::participants::StatusCode::UNKNOWN:               return "unknown";
  case eprosima::ddsenabler::participants::StatusCode::ACCEPTED:              return "accepted";
  case eprosima::ddsenabler::participants::StatusCode::EXECUTING:             return "executing";
  case eprosima::ddsenabler::participants::StatusCode::CANCELING:             return "canceling";
  case eprosima::ddsenabler::participants::StatusCode::SUCCEEDED:             return "succeeded";
  case eprosima::ddsenabler::participants::StatusCode::CANCELED:              return "canceled";
  case eprosima::ddsenabler::participants::StatusCode::ABORTED:               return "aborted";
  case eprosima::ddsenabler::participants::StatusCode::REJECTED:              return "rejected";
  case eprosima::ddsenabler::participants::StatusCode::TIMEOUT:               return "timeout";
  case eprosima::ddsenabler::participants::StatusCode::FAILED:                return "failed";
  case eprosima::ddsenabler::participants::StatusCode::CANCEL_REQUEST_FAILED: return "cancelRequestFailed";
  }
  return "unknown";
}



// -----------------------------------------------------------------------------
//
// ddsActionBuildSubAttribute -
//
KjNode* ddsActionBuildSubAttribute
(
  const char*                                       subName,
  KjNode*                                           payloadValue,
  const char*                                       instanceHandleId,
  const char*                                       participantId,
  const char*                                       ddsDataType,
  int64_t                                           publishedAt
)
{
  KjNode* sub      = kjObject(orionldState.kjsonP, subName);
  KjNode* typeNode = kjString(orionldState.kjsonP, "type", "Property");

  if (payloadValue != NULL)
    payloadValue->name = (char*) "value";

  kjChildAdd(sub, typeNode);
  if (payloadValue != NULL)
    kjChildAdd(sub, payloadValue);

  // The goal is already identified by the instance's datasetId ("urn:goal:<uuid>"),
  // so no per-envelope goalId is stamped here — it would be pure duplication.

  if (instanceHandleId != NULL) kjChildAdd(sub, ddsReplyStringPropertyNode("instanceHandleId", instanceHandleId));
  if (participantId    != NULL) kjChildAdd(sub, ddsReplyStringPropertyNode("participantId",    participantId));
  if (ddsDataType      != NULL) kjChildAdd(sub, ddsReplyStringPropertyNode("ddsDataType",      ddsDataType));

  kjChildAdd(sub, ddsReplyIntegerPropertyNode("publishedAt", (long long) publishedAt));

  return sub;
}
