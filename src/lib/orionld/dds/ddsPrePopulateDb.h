#ifndef SRC_LIB_ORIONLD_DDS_DDSPREPOPULATEDB_H_
#define SRC_LIB_ORIONLD_DDS_DDSPREPOPULATEDB_H_

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
extern "C"
{
#include "kjson/KjNode.h"                                   // KjNode
}



// -----------------------------------------------------------------------------
//
// DdsConceptType -
//
typedef enum DdsConceptType
{
  DdsTopics,
  DdsServices,
  DdsActions
} DdsConceptType;



// -----------------------------------------------------------------------------
//
// ddsPrePopulateDb -
//
extern void ddsPrePopulateDb(DdsConceptType type, KjNode* configNode);



// -----------------------------------------------------------------------------
//
// ddsServicesPopulateFromConfig -
//
// Walks 'dds.ngsild.services' from the config tree and synchronously creates
// the broker's in-memory DdsService linked list. Must run before the DDS
// Enabler is created so that announce_service() can be called at startup
// (before discovery completes).
//
extern void ddsServicesPopulateFromConfig(KjNode* servicesNode);

#endif  // SRC_LIB_ORIONLD_DDS_DDSPREPOPULATEDB_H_
