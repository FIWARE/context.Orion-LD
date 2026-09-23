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
#include <link.h>                                                // dl_iterate_phdr, dl_phdr_info
#include <limits.h>                                              // PATH_MAX
#include <stdlib.h>                                              // realpath
#include <string.h>                                              // strrchr, strstr, strncmp

#include <string>                                                // std::string

#include <fastdds/config.hpp>                                    // FASTDDS_VERSION_STR
#include <fastcdr/config.h>                                      // FASTCDR_VERSION_STR
#include <ddsenabler/library/config.h>                           // DDSENABLER_VERSION_MAJOR, DDSENABLER_VERSION_MINOR

#include "orionld/dds/ddsLibVersions.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// SoVersionQuery - what soVersionSeen looks for, and what it found
//
typedef struct SoVersionQuery
{
  const char*  prefix;                                           // "libddsenabler.so."
  std::string  version;                                          // "1.2.2", or empty
} SoVersionQuery;



// -----------------------------------------------------------------------------
//
// soVersionSeen - one loaded shared object, offered by dl_iterate_phdr
//
static int soVersionSeen(struct dl_phdr_info* infoP, size_t size, void* dataP)
{
  SoVersionQuery* queryP = (SoVersionQuery*) dataP;

  if ((infoP->dlpi_name == NULL) || (infoP->dlpi_name[0] == 0) || (queryP->version.empty() == false))
    return 0;

  const char* slashP = strrchr(infoP->dlpi_name, '/');
  const char* baseP  = (slashP != NULL)? slashP + 1 : infoP->dlpi_name;

  if (strncmp(baseP, queryP->prefix, strlen(queryP->prefix)) != 0)
    return 0;

  //
  // ⭐ THE LOADED PATH IS THE SONAME, AND THE SONAME IS NOT THE VERSION.
  // The loader records what it was asked for - libddsenabler.so.1 - which is a
  // symlink carrying only the major. Resolving it reaches the real file, and
  // the real file's name carries all of it.
  //
  char        resolved[PATH_MAX];
  const char* pathP = (realpath(infoP->dlpi_name, resolved) != NULL)? resolved : infoP->dlpi_name;
  const char* soP   = strstr(pathP, ".so.");

  if (soP != NULL)
    queryP->version = soP + 4;

  return 1;                                                      // found - stop walking
}



// -----------------------------------------------------------------------------
//
// soVersion - the version in a loaded library's file name, else the fallback
//
static const char* soVersion(const char* prefix, const char* fallback)
{
  SoVersionQuery query;

  query.prefix = prefix;

  dl_iterate_phdr(soVersionSeen, &query);

  //
  // Kept for the life of the process: the callers hand the result straight to
  // kjString and to KT_I, neither of which copies.
  //
  static std::string cache[3];
  static int         cacheIx = 0;

  if (query.version.empty() == true)
    return fallback;

  cache[cacheIx] = query.version;

  return cache[cacheIx++].c_str();
}



// -----------------------------------------------------------------------------
//
// ddsFastDdsVersion -
//
const char* ddsFastDdsVersion(void)
{
  static const char* version = soVersion("libfastdds.so.", FASTDDS_VERSION_STR);
  return version;
}



// -----------------------------------------------------------------------------
//
// ddsFastCdrVersion -
//
const char* ddsFastCdrVersion(void)
{
  static const char* version = soVersion("libfastcdr.so.", FASTCDR_VERSION_STR);
  return version;
}



// -----------------------------------------------------------------------------
//
// ddsEnablerVersion -
//
// ⚠ The fallback is MAJOR.MINOR and nothing more - that is all the Enabler's
// config.h defines. It cannot distinguish 1.2.0 from 1.2.2, which is why the
// file name is asked first.
//
const char* ddsEnablerVersion(void)
{
  static std::string  fallback = std::to_string(DDSENABLER_VERSION_MAJOR) + "." + std::to_string(DDSENABLER_VERSION_MINOR);
  static const char*  version  = soVersion("libddsenabler.so.", fallback.c_str());

  return version;
}
