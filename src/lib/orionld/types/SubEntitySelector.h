#ifndef SRC_LIB_ORIONLD_TYPES_SUBENTITYSELECTOR_H_
#define SRC_LIB_ORIONLD_TYPES_SUBENTITYSELECTOR_H_

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
#include <regex.h>                                               // regex_t

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}



// -----------------------------------------------------------------------------
//
// SubEntitySelector - one item of a Subscription's "entities" array, compiled
//
// 'id', 'idPattern' and 'type' all point INSIDE the owning SubCacheItem's
// subTree - the tree is the source of truth, this is just the compiled form of
// it. Only the patterns need compiling (into 'idRegex'/'typeRegex'); 'id' and a
// plain 'type' are string comparisons and are kept here to save a kjLookup per
// alteration.
//
// NGSI-LD has no "type pattern" - 'typeRegexP' is only ever set for a
// subscription that came in over NGSIv2, where "typePattern" is part of the API.
//
typedef struct SubEntitySelector
{
  KjNode*                    owner;        // The entity-selector object inside subTree
  char*                      id;           // NULL if the selector uses idPattern
  char*                      idPattern;    // NULL if the selector uses id
  regex_t                    idRegex;      // Compiled 'idPattern' - only valid if idRegexP != NULL
  regex_t*                   idRegexP;     // &idRegex, or NULL if the idPattern didn't compile (matches nothing)
  char*                      type;         // NULL: any type matches
  regex_t                    typeRegex;    // Compiled 'type' as a pattern - only valid if typeRegexP != NULL
  regex_t*                   typeRegexP;   // &typeRegex, or NULL if 'type' is not a pattern (NGSI-LD: always NULL)
  struct SubEntitySelector*  next;
} SubEntitySelector;

#endif  // SRC_LIB_ORIONLD_TYPES_SUBENTITYSELECTOR_H_
