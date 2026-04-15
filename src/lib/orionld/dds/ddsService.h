#ifndef SRC_LIB_ORIONLD_DDS_DDSSERVICE_H_
#define SRC_LIB_ORIONLD_DDS_DDSSERVICE_H_

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

#include "orionld/types/DdsService.h"                       // DdsService



// -----------------------------------------------------------------------------
//
// ddsService
//
// sync == false: fire-and-forget; dsiOut ignored. Returns true if the DDS
// send succeeded (no wait for the reply).
// sync == true : blocks up to ddsSyncTimeoutMs for the reply. On success,
// '*dsiOut' is set to the reply-filled, unlinked instance and the caller
// must call ddsInstanceFree after consuming it. On failure returns false
// with orionldError already set (503 on send failure, 504 on timeout).
extern bool ddsService(DdsService* serviceP, KjNode* attributeValueP, bool sync = false, DdsServiceInstance** dsiOut = NULL);

#endif  // SRC_LIB_ORIONLD_DDS_DDSSERVICE_H_
