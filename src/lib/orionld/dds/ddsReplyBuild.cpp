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
#include <string.h>                                              // strncmp

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject, kjString, kjInteger, kjChildAdd
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/dds/ddsReplyBuild.h"                           // Own interface



KjNode* ddsReplyStringPropertyNode(const char* name, const char* value)
{
  KjNode* prop  = kjObject(orionldState.kjsonP, name);
  KjNode* tNode = kjString(orionldState.kjsonP, "type",  "Property");
  KjNode* vNode = kjString(orionldState.kjsonP, "value", (value != NULL)? value : "");

  kjChildAdd(prop, tNode);
  kjChildAdd(prop, vNode);
  return prop;
}



KjNode* ddsReplyIntegerPropertyNode(const char* name, long long value)
{
  KjNode* prop  = kjObject(orionldState.kjsonP, name);
  KjNode* tNode = kjString(orionldState.kjsonP, "type",  "Property");
  KjNode* vNode = kjInteger(orionldState.kjsonP, "value", value);

  kjChildAdd(prop, tNode);
  kjChildAdd(prop, vNode);
  return prop;
}



KjNode* ddsReplyExtractMetadata
(
  KjNode*       replyTree,
  const char**  participantIdP,
  const char**  ddsDataTypeP,
  const char**  instanceHandleIdP
)
{
  *participantIdP    = NULL;
  *ddsDataTypeP      = NULL;
  *instanceHandleIdP = NULL;

  if ((replyTree == NULL) || (replyTree->type != KjObject))
    return NULL;

  KjNode* idNode = kjLookup(replyTree, "id");
  if ((idNode != NULL) && (idNode->type == KjString))
    *participantIdP = idNode->value.s;

  KjNode* rrNode = NULL;
  for (KjNode* child = replyTree->value.firstChildP; child != NULL; child = child->next)
  {
    if ((child->name != NULL) && (strncmp(child->name, "rr/", 3) == 0))
    {
      rrNode = child;
      break;
    }
  }

  if ((rrNode == NULL) || (rrNode->type != KjObject))
    return NULL;

  KjNode* typeNode = kjLookup(rrNode, "type");
  if ((typeNode != NULL) && (typeNode->type == KjString))
    *ddsDataTypeP = typeNode->value.s;

  KjNode* dataNode = kjLookup(rrNode, "data");
  if ((dataNode == NULL) || (dataNode->type != KjObject))
    return NULL;

  KjNode* instanceHandleValueNode = dataNode->value.firstChildP;
  if (instanceHandleValueNode == NULL)
    return NULL;

  *instanceHandleIdP = instanceHandleValueNode->name;
  return instanceHandleValueNode;
}



KjNode* ddsReplyBuildSubAttribute
(
  const char*  subName,
  KjNode*      payloadValue,
  uint64_t     requestId,
  const char*  instanceHandleId,
  const char*  participantId,
  const char*  ddsDataType,
  int64_t      publishedAt
)
{
  KjNode* sub      = kjObject(orionldState.kjsonP, subName);
  KjNode* typeNode = kjString(orionldState.kjsonP, "type", "Property");

  payloadValue->name = (char*) "value";

  kjChildAdd(sub, typeNode);
  kjChildAdd(sub, payloadValue);

  kjChildAdd(sub, ddsReplyIntegerPropertyNode("requestId",        (long long) requestId));
  if (instanceHandleId != NULL) kjChildAdd(sub, ddsReplyStringPropertyNode("instanceHandleId", instanceHandleId));
  if (participantId    != NULL) kjChildAdd(sub, ddsReplyStringPropertyNode("participantId",    participantId));
  if (ddsDataType      != NULL) kjChildAdd(sub, ddsReplyStringPropertyNode("ddsDataType",      ddsDataType));
  kjChildAdd(sub, ddsReplyIntegerPropertyNode("publishedAt",      (long long) publishedAt));

  return sub;
}
