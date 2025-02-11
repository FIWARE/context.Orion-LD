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
#include <unistd.h>                                         // NULL

extern "C"
{
#include "kbase/kFileRead.h"                                // kFileRead
#include "kjson/kjson.h"                                    // Kjson
#include "kjson/kjParse.h"                                  // kjParse
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
}

#include "orionld/config/configLoad.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// configTree -
//
KjNode* configTree = NULL;



// -----------------------------------------------------------------------------
//
// configLoad -
//
int configLoad(Kjson* kjP, const char* configFile)
{
  char* buf    = NULL;
  int   bufLen = 0;

  if (kFileRead((char*) "", (char*) configFile, &buf, &bufLen) != 0)
    KT_RE(1, "Error reading the configuration file '%s'", configFile);

  configTree = kjParse(kjP, buf);
  if (configTree == NULL)
    KT_RE(1, "Error parsing the configuration file '%s'", configFile);

  return 0;
}
