#ifndef SRC_LIB_ORIONLD_WS_WSENDPOINTURI_H_
#define SRC_LIB_ORIONLD_WS_WSENDPOINTURI_H_

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
// WS_ENDPOINT_URI_PREFIX - a WS subscription's notification::endpoint::uri
//
// A subscription created over a WebSocket has no endpoint to call back to - the
// notification goes out over the very connection it was created on. There is
// still a 'uri', as the API demands one, and it NAMES that connection:
//
//   urn:ngsi-ld:ws:<file descriptor>
//
// A URN, because it identifies rather than locates - and it carries the file
// descriptor, which is what tells two WS subscriptions apart.
//
// The file descriptor is only meaningful while the broker runs, so this prefix is
// also how a subscription left behind by a crashed broker is recognized at
// startup (no WS connection can have survived) - see subCacheCreate.
//
#define WS_ENDPOINT_URI_PREFIX     "urn:ngsi-ld:ws:"
#define WS_ENDPOINT_URI_PREFIX_LEN 15

#endif  // SRC_LIB_ORIONLD_WS_WSENDPOINTURI_H_
