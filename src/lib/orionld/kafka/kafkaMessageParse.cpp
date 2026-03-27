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
* Author: Carsten Frey
*/
#include <stdlib.h>                                            // malloc
#include <string.h>                                            // memcpy

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjson.h"                                       // Kjson
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/kafka/kafkaMessageParse.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// kafkaMessageParse - parse a Kafka message payload into a KjNode tree
//
// The payload must be valid NGSI-LD JSON: either a single entity (object)
// or an array of entities.
//
KjNode* kafkaMessageParse(Kjson* kjsonP, const char* payload, int payloadLen)
{
  // Make a mutable copy for kjParse (it modifies the input buffer)
  // Using malloc instead of kaAlloc because Kafka payloads can exceed the kalloc arena's 64KB block size
  char* buf = (char*) malloc(payloadLen + 1);
  if (buf == NULL)
  {
    KT_E("kafkaMessageParse: out of memory allocating %d bytes", payloadLen + 1);
    return NULL;
  }
  orionldStateDelayedFreeEnqueue(buf);

  memcpy(buf, payload, payloadLen);
  buf[payloadLen] = 0;

  KjNode* tree = kjParse(kjsonP, buf);
  if (tree == NULL)
  {
    KT_W("kafkaMessageParse: error parsing JSON payload");
    return NULL;
  }

  // Validate: must be an object (single entity) or an array (batch of entities)
  if (tree->type != KjObject && tree->type != KjArray)
  {
    KT_W("kafkaMessageParse: payload must be a JSON Object or Array, got type %d", tree->type);
    return NULL;
  }

  // For a single entity (object), validate that 'id' and 'type' are present
  if (tree->type == KjObject)
  {
    if (kjLookup(tree, "id") == NULL)
    {
      KT_W("kafkaMessageParse: entity missing 'id' field");
      return NULL;
    }
    if (kjLookup(tree, "type") == NULL)
    {
      KT_W("kafkaMessageParse: entity missing 'type' field");
      return NULL;
    }
  }

  // buf is freed via orionldStateDelayedFreeEnqueue when the request/batch cycle ends
  return tree;
}
