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
#include <stdio.h>                                          // snprintf
#include <stdlib.h>                                         // getenv
#include <unistd.h>                                         // access
#include <errno.h>                                          // errno

extern "C"
{
#include "kjson/kjson.h"                                    // Kjson
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
}

#include "orionld/config/configLoad.h"                      // configLoad
#include "orionld/config/configInit.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// configInit -
//
void configInit(Kjson* kjP, char* configFile)
{
  if (configFile[0] == 0)
  {
    char* home = getenv("HOME");

    if (home != NULL)
    {
      snprintf(configFile, 511, "%s/.orionld", home);
      if (access(configFile, R_OK) != 0)
        return;  // It's OK to not have a config file
    }
  }

  errno = 0;
  if (configLoad(kjP, configFile) != 0)
    KT_X(1, "Error reading/parsing the config file '%s'", configFile);  // Not OK to have a bad config file
}
