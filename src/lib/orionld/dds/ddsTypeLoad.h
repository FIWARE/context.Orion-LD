#ifndef SRC_LIB_ORIONLD_DDS_DDSTYPELOAD_H_
#define SRC_LIB_ORIONLD_DDS_DDSTYPELOAD_H_

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
#include <stdint.h>                                              // uint32_t



// -----------------------------------------------------------------------------
//
// ddsTypesDirectorySet -
//
// Configure the directory from which DDS type binary blobs (.bin files) are
// loaded on demand. The directory is shared between the broker (loaded from
// the 'dds.ngsild.typesDirectory' config-file entry at startup) and the
// ftClient test daemon (set via POST /dds/type).
//
extern void ddsTypesDirectorySet(const char* path);



// -----------------------------------------------------------------------------
//
// ddsTypeLoad -
//
// Load the binary type representation for 'typeName' from the configured
// types directory using eProsima's safe filename convention (':' / '/' / '\'
// replaced with '_'). On success, '*dataP' points to a freshly malloc'd buffer
// of '*sizeP' bytes that the caller (or DDS Enabler) takes ownership of.
//
// Returns false (and leaves *dataP / *sizeP untouched) if the directory has
// not been set, the file does not exist, or the read fails.
//
extern bool ddsTypeLoad(const char* typeName, unsigned char** dataP, uint32_t* sizeP);

#endif  // SRC_LIB_ORIONLD_DDS_DDSTYPELOAD_H_
