/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                       // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjBuilder.h"                                     // kjChildRemove
}

#include "orionld/types/DdsType.h"                               // DdsType
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/config/configAttributeToDdsTopic.h"            // configAttributeToDdsTopic
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/kjTree/kjChildCount.h"                         // kjChildCount
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler
#include "orionld/dds/ddsTypes.h"                                // ddsTypeLookupByTopic
#include "orionld/dds/kjTreeLog.h"                               // kjTreeLog2
#include "orionld/dds/ddsPublishAttribute.h"                     // Own interface



// ----------------------------------------------------------------------------
//
// itemSerialize -
//
const char* itemSerialize(KjNode* itemP)
{
  char  tipo = 'X';
  char* name = itemP->name;
  char  arrayName[128];

  if      (itemP->type == KjInt)     tipo = 'L';
  else if (itemP->type == KjFloat)   tipo = 'D';
  else if (itemP->type == KjString)  tipo = 'S';
  else if (itemP->type == KjBoolean) tipo = 'B';
  else if (itemP->type == KjArray)
  {
    int arrayItems = kjChildCount(itemP);

    snprintf(arrayName, sizeof(arrayName) - 1, "%s[%d]", name, arrayItems);
    name = arrayName;

    // Assume all children are of the same JSON type ...
    if      (itemP->value.firstChildP->type == KjInt)     tipo = 'L';
    else if (itemP->value.firstChildP->type == KjFloat)   tipo = 'D';
    else if (itemP->value.firstChildP->type == KjString)  tipo = 'S';
    else if (itemP->value.firstChildP->type == KjBoolean) tipo = 'B';
    else
      tipo = 'C';
  }
  else
    return "CompoundNotSupported";

  int   size = strlen(name) + 5;  // ;<tipo>:<name>;<\0>
  char* buf  = kaAlloc(&orionldState.kalloc, size);

  snprintf(buf, size, ";%c:%s;", tipo, name);
  return buf;
}



// ----------------------------------------------------------------------------
//
// ddsPublishAttribute -
//
// What is published over DDS is the "value" field of the attribute.
// For now, sub-attributes are not used in DDS.
//
void ddsPublishAttribute(char* topic, const char* attrName, KjNode* attrP, bool isValue)
{
  KT_T(StDds, "Pushing attribute '%s' (%s) to DDS topic '%s'", attrName, attrP->name, topic);

  KjNode* valueP = (isValue == true)? attrP : kjLookup(attrP, "value");

  if (valueP == NULL)
    KT_RVE("Attribute '%s' doesn't have a value!'", attrName);

  if (valueP->type != KjObject)
  {
    KT_W("Can't publish a JSON '%s', only attribute values that are JSON Objects", kjValueType(valueP->type));
    return;
  }

  kjTreeLog2(valueP, "Attr Value", StDds);

  if ((isValue == false) && (valueP == NULL))
    KT_RVE("The field named 'value' missing in the merged attribute");

  if (topic == NULL)
  {
    char* shortName = orionldContextItemAliasLookup(orionldState.contextP, attrName, NULL, NULL);

    topic = configAttributeToDdsTopic(shortName);
    if (topic == NULL)
    {
      KT_T(StDds, "Nothing to be published (attribute '%s' not in config file)", shortName);
      return;
    }
  }

  //
  // Strip something away?
  //
  DdsType* typeP = ddsTypeLookupByTopic(topic);
  KjNode*  itemP = valueP->value.firstChildP;
  KjNode*  next;
  int      itemsToPublish = 0;
  int      itemsToIgnore  = 0;

  while (itemP != NULL)
  {
    next = itemP->next;

    const char* item = itemSerialize(itemP);
    if (strstr(typeP->type, item) == NULL)
    {
      KT_T(StDdsTypes, "Not publishing the value field '%s' as it is not part of the DDS type '%s' (%s)", item, typeP->typeName, typeP->type);
      kjChildRemove(valueP, itemP);
      ++itemsToIgnore;
    }
    else
      ++itemsToPublish;

    itemP = next;
  }

  if (itemsToPublish == 0)
  {
    KT_T(StDdsTypes, "Nothing to publish, %d value fields ignored", itemsToIgnore);
    return;
  }

  int   serialiedSize = kjFastRenderSize(valueP);
  char  buf[1024];
  char* bufP    = buf;
  int   bufSize = 1024;

  if (serialiedSize > bufSize - 100)
  {
    bufP    = kaAlloc(&orionldState.kalloc, serialiedSize + 100);
    bufSize = serialiedSize + 100;
  }

  kjFastRender(valueP, bufP);
  KT_T(StDds, "Publishing attribute '%s' on DDS topic '%s'. Value: %s", attrName, topic, bufP);

  ddsEnabler->publish(topic, bufP);
}
