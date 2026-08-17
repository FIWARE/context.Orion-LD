#ifndef SRC_LIB_ORIONLD_TYPES_REGCACHEITEM_H_
#define SRC_LIB_ORIONLD_TYPES_REGCACHEITEM_H_

/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include <stdint.h>                                              // types: uint32_t, ...

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/RegistrationMode.h"                      // RegistrationMode
#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/types/RegIdPattern.h"                          // RegIdPattern



// -----------------------------------------------------------------------------
//
// RegDeltas -
//
typedef struct RegDeltas
{
  uint32_t timesSent;
  uint32_t timesFailed;
  double   lastSuccess;
  double   lastFailure;
} RegDeltas;



// -----------------------------------------------------------------------------
//
// RegCacheItem -
//
typedef struct RegCacheItem
{
  KjNode*               regTree;
  char*                 regId;         // Set when creating registration - points inside regTree
  RegDeltas             deltas;

  //
  // Lifetime - see regCacheSem.h
  //
  // A DistOp keeps its RegCacheItem* for the whole of a forwarded request, which outlives the walk
  // of the cache that produced it. 'refs' counts those holders: while it is non-zero the item must
  // not be freed, even when the registration it came from is deleted. A DELETE arriving mid-forward
  // therefore unlinks the item and sets 'removed'; the last holder to unpin it does the freeing.
  //
  struct RegCache*      owner;         // The cache this item lives in - so a holder can pin/unpin with the item alone
  uint32_t              refs;          // Pin count. Incremented under the read lock, decremented under the write lock
  bool                  removed;       // Unlinked from the list, waiting for the last unpin to free it

  // "Shortcuts" and transformed info, all copies from the regTree - for improved performance
  RegistrationMode      mode;
  uint64_t              opMask;
  OrionldContext*       contextP;           // Set when creating/patching registration
  bool                  acceptJsonld;       // application/ld+json
  char*                 ipAndPort;          // IP:port - for X-Forwarded-For
  char*                 rest;               // What comes after IP:port
  RegIdPattern*         idPatternRegexList;
  char*                 hostAlias;          // Broker identity - for the Via header (replacing X-Forwarded-For)
  bool                  localOnly;          // Forwarded reqs to include local=true if this field is true

  struct RegCacheItem*  next;
} RegCacheItem;

#endif  // SRC_LIB_ORIONLD_TYPES_REGCACHEITEM_H_
