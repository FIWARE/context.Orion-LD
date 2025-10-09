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
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
#include "kjson/kjBuilder.h"                                     // kjChildRemove
}

#include "orionld/types/DdsType.h"                               // DdsType
#include "orionld/common/traceLevels.h"                          // KT_T trace levels
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/config/configAttributeToDdsTopic.h"            // configAttributeToDdsTopic
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/kjTree/kjChildCount.h"                         // kjChildCount
#include "orionld/dds/ddsInit.h"                                 // ddsEnabler
#include "orionld/dds/ddsTypes.h"                                // ddsTypeLookupByTopic, typeItemArraySort, typeItemArraySize, typeItemArraySerialize
#include "orionld/dds/kjTreeLog.h"                               // kjTreeLog2
#include "orionld/dds/ddsServiceLookup.h"                        // ddsServiceLookup
#include "orionld/dds/ddsServiceLookupByAttributeName.h"         // ddsServiceLookupByAttributeName
#include "orionld/dds/ddsService.h"                              // ddsService
#include "orionld/dds/ddsPublishAttribute.h"                     // Own interface


#if 0
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



// -----------------------------------------------------------------------------
//
// kjDdsType -
//
static char* kjDdsType(KjNode* valueP, char* buf, int bufSize)
{
  // 1. No of members in valueP
  // 2. Allocate array of char*
  // 3. Loop valueP and fill in the array (e.g. "abc": 12 => L:12)  => Similar to ddsTypeItem
  // 4. Sort the array  => typeItemArraySort
  // 5. Make sure the size of 'buf' is enough => typeItemArraySize
  // 6. Render the array info 'buf'

  int    members = kjChildCount(valueP);
  char** itemV   = (char**) kaAlloc(&orionldState.kalloc, members * sizeof(char*));
  int    itemNo  = 0;

  for (KjNode* itemP = valueP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    int   len  = strlen(itemP->name) + 10;
    char* item = kaAlloc(&orionldState.kalloc, len);
    char  type = 'C';

    if      (itemP->type == KjString)  type = 'S';
    else if (itemP->type == KjInt)     type = 'L';
    else if (itemP->type == KjFloat)   type = 'D';
    else if (itemP->type == KjBoolean) type = 'B';

    if (itemP->type == KjArray)
    {
      char    type  = 'C';
      KjNode* child = itemP->value.firstChildP;

      if      (child->type == KjString)  type = 'S';
      else if (child->type == KjInt)     type = 'L';
      else if (child->type == KjFloat)   type = 'D';
      else if (child->type == KjBoolean) type = 'B';

      int arrayItems = kjChildCount(itemP);
      snprintf(item, len - 1, "%c:%s[%d];", type, itemP->name, arrayItems);
    }
    else
      snprintf(item, len - 1, "%c:%s;", type, itemP->name);

    itemV[itemNo++] = item;
  }

  typeItemArraySort(itemV, itemNo);

  int sizeNeeded = typeItemArraySize(itemV, itemNo);

  if (sizeNeeded >= bufSize)
    KT_X(1, "Need %d bytes to serialize DDS type, I only have - please fix and recompile!", sizeNeeded, bufSize);

  return typeItemArraySerialize(buf, itemV, itemNo);
}
#endif



// ----------------------------------------------------------------------------
//
// ddsPublishAttribute -
//
// What is published over DDS is the "value" field of the attribute.
// For now, sub-attributes are not used in DDS.
//
void ddsPublishAttribute(const char* entityId, char* attrShortName, KjNode* attrP, bool isValue)
{
  KjNode* valueP = (isValue == true)? attrP : kjLookup(attrP, "value");
  if (valueP == NULL)
    KT_RVE("Attribute '%s' doesn't have a value!'", attrShortName);

  if (valueP->type != KjObject)
  {
    KT_W("Can't publish a JSON '%s', only attribute values that are JSON Objects", kjValueType(valueP->type));
    return;
  }

  kjTreeLog2(valueP, "Attr Value", StDds);

  //
  // Might be 'attrShortName' is not a shortname ...
  // In the worst case, it's even a long name with '=' instead of '.' (coming from the database)
  //
  char* attrLongName  = kaStrdup(&orionldState.kalloc, attrShortName);
  eqForDot(attrLongName);
  attrShortName       = orionldContextItemAliasLookup(orionldState.contextP, attrShortName, NULL, NULL);
  char* topic         = configAttributeToDdsTopic(entityId, attrShortName);

  KT_T(StDdsService, "attrShortName:  '%s'", attrShortName);

  if (topic == NULL)
  {
    KT_T(StDdsService, "No topic found, might be a Service");
    DdsService* sP = ddsServiceLookupByAttributeName(attrShortName);
    KT_T(StDdsService, "service at %p", sP);
    KT_T(StDdsService, "attrP at %p", attrP);

    if (sP == NULL)
      KT_T(StDds, "Nothing to be published (attribute '%s' not in config file)", attrShortName);
    else
    {
      KjNode* attributeValueP = kjLookup(attrP, "value");
      KT_T(StDds, "attributeValueP at %p", attributeValueP);
      ddsService(sP, attributeValueP);
    }

    return;
  }

  KT_T(StDds, "Pushing attribute '%s' (%s) to DDS topic '%s'", attrShortName, attrP->name, topic);

  if ((isValue == false) && (valueP == NULL))
    KT_RVE("The field named 'value' missing in the merged attribute");

#if 0
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

  // Now, is the value struct according to DDS ?
  char  serializedV[1024];  // Hopefully enough
  char* serialized = kjDdsType(valueP, serializedV, sizeof(serializedV));

  if (strcmp(serialized, typeP->type) != 0)
    KT_RVE("Not publishing attribute '%s' of entity '%s' on DDS as types differ: expected from DDS: '%s', got via HTTP: '%s'", attrShortName, entityId, typeP->type, serialized);
#endif

  //
  // All good, lets serialize and send to the DDS Enabler
  //
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

  KT_T(StDds, "Publishing attribute '%s' on DDS topic '%s'. Value: %s", attrShortName, topic, bufP);
  ddsEnabler->publish(topic, bufP);
}
