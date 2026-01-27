#ifndef TEST_FUNCTIONALTEST_FTCLIENT_POSTDDSTYPE_H_
#define TEST_FUNCTIONALTEST_FTCLIENT_POSTDDSTYPE_H_

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
#include <stdint.h>                                          // uint32_t

extern "C"
{
#include "kjson/KjNode.h"                                    // KjNode
}



// -----------------------------------------------------------------------------
//
// DdsTypeData - stored binary type data
//
typedef struct DdsTypeData
{
  unsigned char*  data;
  uint32_t        size;
} DdsTypeData;



// -----------------------------------------------------------------------------
//
// ddsTypeLookup - lookup a type by name
//
extern DdsTypeData* ddsTypeLookup(const char* typeName);



// -----------------------------------------------------------------------------
//
// postDdsType -
//
extern KjNode* postDdsType(int* statusCodeP);

#endif  // TEST_FUNCTIONALTEST_FTCLIENT_POSTDDSTYPE_H_
