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
#include <stdio.h>                                          // sprintf
#include <string.h>                                         // strdup, strcmp, strncmp
#include <stdint.h>                                         // types: uint64_t, ...
#include <stdlib.h>                                         // malloc

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
}

#include "orionld/types/DdsType.h"                          // DdsType
#include "orionld/common/traceLevels.h"                     // StDdsTypeCache, ...
#include "orionld/dds/ddsTypes.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// ddsTypeItem -
//
static char* ddsTypeItem(char* typeItemP)
{
  char* out = typeItemP;
  int   len = strlen(typeItemP) - 1;

  if      (strncmp(typeItemP, "string ",  7) == 0) snprintf(out, len, "S:%s", &typeItemP[7]);
  else if (strncmp(typeItemP, "int ",     4) == 0) snprintf(out, len, "I:%s", &typeItemP[4]);
  else if (strncmp(typeItemP, "long ",    5) == 0) snprintf(out, len, "L:%s", &typeItemP[5]);
  else if (strncmp(typeItemP, "double ",  7) == 0) snprintf(out, len, "D:%s", &typeItemP[7]);
  else if (strncmp(typeItemP, "boolean ", 8) == 0) snprintf(out, len, "B:%s", &typeItemP[8]);

  return out;
}



// -----------------------------------------------------------------------------
//
// typeItemArraySort -
//
static void typeItemArraySort(char** itemV, int items)
{
  for (int fromIx = 0; fromIx < items; fromIx++)
  {
    int smallIx = fromIx;

    for (int ix = fromIx + 1; ix < items; ix++)
    {
      if (strcmp(itemV[ix], itemV[smallIx]) < 0)  // itemV[ix] < itemV[smallIx] => keep it as smallIx
        smallIx = ix;
    }

    // Interchange smallIx and fromIx
    if (smallIx != fromIx)
    {
      char* saved   = itemV[fromIx];
      itemV[fromIx] = itemV[smallIx];
      itemV[smallIx] = saved;
    }
  }
}



// -----------------------------------------------------------------------------
//
// typeItemArraySize -
//
static int typeItemArraySize(char** itemV, int items)
{
  int size = 0;

  for (int ix = 0; ix < items; ix++)
  {
    int len = strlen(itemV[ix]);

    size += len;
  }

  return size + 2;
}



// -----------------------------------------------------------------------------
//
// typeItemArraySerialize -
//
static char* typeItemArraySerialize(char* s, char** itemV, int items)
{
  int sIx = 1;

  *s=';';
  for (int ix = 0; ix < items; ix++)
  {
    int len = strlen(itemV[ix]);
    strcpy(&s[sIx], itemV[ix]);
    sIx += len;
  }

  return s;
}



// -----------------------------------------------------------------------------
//
// ddsTypeCache -
//
static DdsType* ddsTypeCache = NULL;



// -----------------------------------------------------------------------------
//
// ddsTypeLookup -
//
DdsType* ddsTypeLookup(const char* typeName)
{
  DdsType* typeP = ddsTypeCache;

  while (typeP != NULL)
  {
    if (strcmp(typeP->typeName, typeName) == 0)
      return typeP;

    typeP = typeP->next;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsTypeLookupByTopic -
//
DdsType* ddsTypeLookupByTopic(const char* topic)
{
  DdsType* typeP = ddsTypeCache;

  while (typeP != NULL)
  {
    if ((typeP->topic != NULL) && (strcmp(typeP->topic, topic) == 0))
      return typeP;

    typeP = typeP->next;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsTypeAdd -
//
static void ddsTypeAdd(const char* typeName, char* type)
{
  if (ddsTypeLookup(typeName) != NULL)
    return;

  DdsType* typeP = (DdsType*) malloc(sizeof(DdsType));

  typeP->typeName = strdup(typeName);
  typeP->type     = type;
  typeP->next     = ddsTypeCache;

  ddsTypeCache = typeP;
}



// -----------------------------------------------------------------------------
//
// ddsTypeList -
//
void ddsTypeList(void)
{
  DdsType* typeP = ddsTypeCache;

  KT_T(StDdsTypeCache, "--------------------------------------------------------------------------------");
  KT_T(StDdsTypeCache, "%-20s  %-30s  %s", "typeName", "topic", "serialized");

  while (typeP != NULL)
  {
    KT_T(StDdsTypeCache, "%-20s  %-30s  %s", typeP->typeName, typeP->topic, typeP->type);
    typeP = typeP->next;
  }

  KT_T(StDdsTypeCache, "--------------------------------------------------------------------------------");
}



// -----------------------------------------------------------------------------
//
// ddsTypeNotification -
//
void ddsTypeNotification
(
  const char*           typeName,
  const char*           serializedType,
  const unsigned char*  serializedTypeInternal,
  uint32_t              serializedTypeInternalSize,
  const char*           dataPlaceholder
)
{
  KT_T(StDdsTypes, "----------------------------------------");
  KT_T(StDdsTypes, "Got a type notification:");
  KT_T(StDdsTypes, "o typeName:                    %s", typeName);
  KT_T(StDdsTypes, "o serializedType:              %s", serializedType);
  KT_T(StDdsTypes, "o serializedTypeInternal:      %s", serializedTypeInternal);
  KT_T(StDdsTypes, "o serializedTypeInternalSize:  %d", serializedTypeInternalSize);
  KT_T(StDdsTypes, "o dataPlaceholder:             %s", dataPlaceholder);
  KT_T(StDdsTypes, "Nothing done, for now at least");
  KT_T(StDdsTypes, "----------------------------------------");

  //
  // The type (serializedType) comes in as a string, e.g.:
  //
  // struct NgsildSample
  // {
  //   string s;
  //   long i;
  //   double f;
  //   boolean b;
  //   long ia[2];
  // };
  //
  // However, the typeName we have already. so, we need to skip over the first two lines.
  // After that, each line until the '\n}' needs to be sorted alphabetically and then shortened down

  char* stP  = (char*) serializedType;
  int   nls  = 0;
  char* lineStart = NULL;
  char* itemV[100];
  int   itemIx = 0;

  while (*stP != 0)
  {
    if (*stP == '\n')
    {
      *stP = 0;

      if (nls >= 2)
      {
        if (strcmp(lineStart, "};") != 0)
        {
          // Remove initial ws
          while ((*lineStart == ' ') || (*lineStart == '\t'))
            ++lineStart;

          itemV[itemIx++] = ddsTypeItem(lineStart);
        }
        else
          break;
      }

      lineStart = &stP[1];
      ++nls;
    }

    ++stP;
  }

  typeItemArraySort(itemV, itemIx);
  int   size       = typeItemArraySize(itemV, itemIx);
  char* s          = (char*) malloc(size);
  char* serialized = typeItemArraySerialize(s, itemV, itemIx);

  ddsTypeAdd(typeName, serialized);
}
