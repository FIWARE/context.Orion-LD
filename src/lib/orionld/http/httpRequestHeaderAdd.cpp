/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
#include <unistd.h>                                            // NULL
#include <stdio.h>                                             // snprintf

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kalloc/kaAlloc.h"                                    // kaAlloc
}

#include "orionld/types/HttpKeyValue.h"                        // HttpKeyValue
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/http/httpRequestHeaderAdd.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// httpRequestHeaderAdd -
//
// NOTE:
//   HTTP headers are in an array of HttpKeyValue.
//   the headerP references an index in that array and the caller needs to increment the index
//
void httpRequestHeaderAdd(HttpKeyValue* headerP, const char* key, const char* value, int ivalue)
{
  char* val = (value == NULL)? kaAlloc(&orionldState.kalloc, 16) : (char*) value;

  if (value == NULL)
    snprintf(val, 15, "%d", ivalue);

  KT_T(KtSR, "Adding HTTP header: %s=%s", key, val);
  headerP->key   = (char*) key;
  headerP->value = (char*) val;
}
