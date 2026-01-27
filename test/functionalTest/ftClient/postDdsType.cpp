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
#include <dirent.h>                                          // opendir, readdir, closedir
#include <string.h>                                          // strdup, strstr
#include <stdlib.h>                                          // malloc, free

#include <map>                                               // std::map
#include <string>                                            // std::string

extern "C"
{
#include "ktrace/kTrace.h"                                   // trace messages - ktrace library
#include "kjson/KjNode.h"                                    // KjNode
#include "kjson/kjLookup.h"                                  // kjLookup
#include "kjson/kjBuilder.h"                                 // kjObject, kjString, kjInteger
}

#include "common/orionldState.h"                             // orionldState
#include "common/traceLevels.h"                              // Trace levels for ktrace

#include "ftClient/ftErrorResponse.h"                        // ftErrorResponse
#include "ftClient/postDdsType.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// ddsTypeMap - map of loaded DDS types (typeName -> binary data)
//
static std::map<std::string, DdsTypeData> ddsTypeMap;



// -----------------------------------------------------------------------------
//
// ddsTypeLookup - lookup a type by name
//
DdsTypeData* ddsTypeLookup(const char* typeName)
{
  auto it = ddsTypeMap.find(typeName);
  if (it != ddsTypeMap.end())
    return &it->second;
  return NULL;
}



// -----------------------------------------------------------------------------
//
// typeNameFromFilename - extract type name from .bin filename
//
// The persistence files use double underscore as separator, e.g.:
//   example_interfaces__srv__dds___AddTwoInts_Request_.bin
// becomes:
//   example_interfaces::srv::dds_::AddTwoInts_Request_
//
static char* typeNameFromFilename(const char* filename)
{
  // Remove .bin extension
  char* name = strdup(filename);
  char* dot = strstr(name, ".bin");
  if (dot != NULL)
    *dot = 0;

  // Replace double underscores with ::
  std::string typeName;
  char* p = name;
  while (*p != 0)
  {
    if (p[0] == '_' && p[1] == '_')
    {
      typeName += "::";
      p += 2;
    }
    else
    {
      typeName += *p;
      p++;
    }
  }

  free(name);
  return strdup(typeName.c_str());
}



// -----------------------------------------------------------------------------
//
// loadTypeFromFile - load binary type data from file
//
static bool loadTypeFromFile(const char* filePath, const char* typeName)
{
  FILE* fp = fopen(filePath, "rb");
  if (fp == NULL)
  {
    KT_E("Failed to open type file '%s'", filePath);
    return false;
  }

  // Get file size
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size <= 0)
  {
    KT_E("Empty or invalid type file '%s'", filePath);
    fclose(fp);
    return false;
  }

  // Allocate and read
  unsigned char* data = (unsigned char*) malloc(size);
  if (data == NULL)
  {
    KT_E("Failed to allocate %ld bytes for type '%s'", size, typeName);
    fclose(fp);
    return false;
  }

  size_t bytesRead = fread(data, 1, size, fp);
  fclose(fp);

  if (bytesRead != (size_t) size)
  {
    KT_E("Failed to read type file '%s': expected %ld, got %zu", filePath, size, bytesRead);
    free(data);
    return false;
  }

  // Store in map
  DdsTypeData typeData;
  typeData.data = data;
  typeData.size = (uint32_t) size;

  ddsTypeMap[typeName] = typeData;
  KT_V("Loaded DDS type '%s' (%u bytes)", typeName, typeData.size);

  return true;
}



// -----------------------------------------------------------------------------
//
// postDdsType -
//
// POST /dds/type
// {
//   "path": "/absolute/path/to/types/directory"
// }
//
// Loads all .bin files from the specified directory as DDS type definitions.
// The type name is derived from the filename by replacing __ with ::
//
KjNode* postDdsType(int* statusCodeP)
{
  if (orionldState.requestTree == NULL)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Bad Request", "No payload body");
  }

  KjNode* pathNode = kjLookup(orionldState.requestTree, "path");
  if (pathNode == NULL || pathNode->type != KjString)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Bad Request", "Missing or invalid 'path' field");
  }

  const char* path = pathNode->value.s;
  KT_V("Loading DDS types from path '%s'", path);

  DIR* dir = opendir(path);
  if (dir == NULL)
  {
    *statusCodeP = 400;
    return ftErrorResponse(400, "Bad Request", "Cannot open directory");
  }

  int loadedCount = 0;
  struct dirent* entry;

  while ((entry = readdir(dir)) != NULL)
  {
    // Skip non-.bin files
    if (strstr(entry->d_name, ".bin") == NULL)
      continue;

    // Build full path
    char filePath[1024];
    snprintf(filePath, sizeof(filePath), "%s/%s", path, entry->d_name);

    // Extract type name from filename
    char* typeName = typeNameFromFilename(entry->d_name);

    if (loadTypeFromFile(filePath, typeName))
      loadedCount++;

    free(typeName);
  }

  closedir(dir);

  KT_V("Loaded %d DDS types from '%s'", loadedCount, path);

  *statusCodeP = 201;

  KjNode* response = kjObject(NULL, NULL);
  KjNode* countNode = kjInteger(NULL, "typesLoaded", loadedCount);
  KjNode* pathRespNode = kjString(NULL, "path", path);

  kjChildAdd(response, countNode);
  kjChildAdd(response, pathRespNode);

  return response;
}
