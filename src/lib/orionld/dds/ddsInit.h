#ifndef SRC_LIB_ORIONLD_DDS_DDSINIT_H_
#define SRC_LIB_ORIONLD_DDS_DDSINIT_H_

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
extern "C"
{
#include "kjson/kjson.h"                                    // Kjson
}

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler



// -----------------------------------------------------------------------------
//
// ddsEnabler -
//
extern std::shared_ptr<eprosima::ddsenabler::DDSEnabler>  ddsEnabler;



// -----------------------------------------------------------------------------
//
// ddsSyncTimeoutMs - how long PATCH /entities/{id}?ddsSync=true waits for the
// DDS service reply before giving up and returning 504 Gateway Timeout.
// Configured via 'dds.ngsild.syncTimeoutMs' in the broker config. Defaults
// to 5000ms.
//
extern int64_t ddsSyncTimeoutMs;



// -----------------------------------------------------------------------------
//
// ddsInit -
//
extern int ddsInit(Kjson* kjP);

#endif  // SRC_LIB_ORIONLD_DDS_DDSINIT_H_
