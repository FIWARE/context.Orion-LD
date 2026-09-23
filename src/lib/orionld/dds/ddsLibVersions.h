#ifndef SRC_LIB_ORIONLD_DDS_DDSLIBVERSIONS_H_
#define SRC_LIB_ORIONLD_DDS_DDSLIBVERSIONS_H_

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



// -----------------------------------------------------------------------------
//
// ddsLibVersions - the versions of the DDS libraries this process has LOADED
//
// Every other library the broker links is reported in GET /ngsi-ld/ex/v1/version
// and the DDS stack was the one exception - which is the stack that moves
// fastest, and whose pin was a deleted branch for months. A deployment hitting
// a DDS interoperability problem could not ask its own broker what it was
// running.
//
// ⭐ READ FROM THE LOADED LIBRARY'S FILE NAME, NOT FROM THE HEADERS, and for
// two reasons:
//
//   1. the Enabler's config.h defines DDSENABLER_VERSION_MAJOR and _MINOR and
//      stops there, so the macros cannot tell 1.2.0 from 1.2.2 - exactly the
//      distinction that matters, those being two different DDS stacks;
//   2. a macro says what this was COMPILED against. The file the loader opened
//      says what is RUNNING, and for a version report that is the question.
//
// The strings are computed once and owned here; the caller must not free them.
//
extern const char* ddsFastDdsVersion(void);
extern const char* ddsFastCdrVersion(void);
extern const char* ddsEnablerVersion(void);

#endif  // SRC_LIB_ORIONLD_DDS_DDSLIBVERSIONS_H_
