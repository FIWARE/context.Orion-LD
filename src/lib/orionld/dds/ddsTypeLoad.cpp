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
#include <stdio.h>                                               // FILE, fopen, fread, fclose
#include <stdlib.h>                                              // malloc, free
#include <stdint.h>                                              // uint32_t

#include <string>                                                // std::string

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/traceLevels.h"                          // StDds
#include "orionld/dds/ddsTypeLoad.h"                             // Own interface



// -----------------------------------------------------------------------------
//
// ddsTypesDirectory - directory containing the .bin type representation files
//
// Set once (at broker startup or via ftClient's POST /dds/type) and read on
// every ddsTypeLoad() call.
//
static std::string ddsTypesDirectory;



// -----------------------------------------------------------------------------
//
// ddsTypesDirectorySet -
//
void ddsTypesDirectorySet(const char* path)
{
  if (path == NULL)
    ddsTypesDirectory.clear();
  else
    ddsTypesDirectory = path;
}



// -----------------------------------------------------------------------------
//
// ddsTypeLoad -
//
bool ddsTypeLoad(const char* typeName, unsigned char** dataP, uint32_t* sizeP)
{
  if (ddsTypesDirectory.empty())
  {
    KT_T(StDds, "DDS types directory not set - can't load type '%s'", typeName);
    return false;
  }

  //
  // eProsima's safe_file_name convention: replace ':', '/' and '\' with '_'.
  // (We don't reverse-map the filename, so this lossy mapping is fine.)
  //
  std::string safeName(typeName);
  for (char& c : safeName)
  {
    if ((c == ':') || (c == '/') || (c == '\\'))
      c = '_';
  }

  std::string filePath = ddsTypesDirectory + "/" + safeName + ".bin";

  FILE* fp = fopen(filePath.c_str(), "rb");
  if (fp == NULL)
  {
    KT_T(StDds, "DDS type file not found: '%s' (for type '%s')", filePath.c_str(), typeName);
    return false;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size <= 0)
  {
    KT_E("Empty DDS type file: '%s'", filePath.c_str());
    fclose(fp);
    return false;
  }

  unsigned char* data = (unsigned char*) malloc(size);
  if (data == NULL)
  {
    KT_E("Out of memory allocating %ld bytes for DDS type '%s'", size, typeName);
    fclose(fp);
    return false;
  }

  size_t bytesRead = fread(data, 1, size, fp);
  fclose(fp);

  if (bytesRead != (size_t) size)
  {
    KT_E("Short read on DDS type file '%s': expected %ld, got %zu", filePath.c_str(), size, bytesRead);
    free(data);
    return false;
  }

  *dataP = data;
  *sizeP = (uint32_t) size;

  KT_T(StDds, "Loaded DDS type '%s' from '%s' (%u bytes)", typeName, filePath.c_str(), *sizeP);
  return true;
}
