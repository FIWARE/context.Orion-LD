#ifndef SRC_LIB_ORIONLD_COMMON_CORRELATORGET_H_
#define SRC_LIB_ORIONLD_COMMON_CORRELATORGET_H_

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
* Author: Carsten Frey
*/



// -----------------------------------------------------------------------------
//
// correlatorGet - return the resolved correlator for the current request
//
// The correlator comes from the "NGSILD-Correlator" request header (falling back to
// "Fiware-Correlator"); if neither is present a fresh one is generated
// ("urn:ngsi-ld:correlator:<uuid>"). Any notification suffix ("; cbnotif=N") is stripped,
// so only the root is returned. The value is resolved once per request and cached.
//
// All writes of a single request share this value: it is stored on the entity
// ("lastCorrelator" in MongoDB) and on every TRoE row, so all attributes written together
// can be grouped reliably.
//
extern char* correlatorGet(void);



// -----------------------------------------------------------------------------
//
// correlatorClientProvided - true if the client supplied a correlator header
//
// Used to decide whether to mirror the correlator into the entity's MongoDB
// 'lastCorrelator' field (only client-supplied correlators are mirrored;
// generated ones live only on the TRoE rows).
//
extern bool correlatorClientProvided(void);

#endif  // SRC_LIB_ORIONLD_COMMON_CORRELATORGET_H_
