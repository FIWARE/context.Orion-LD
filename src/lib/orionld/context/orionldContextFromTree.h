#ifndef SRC_LIB_ORIONLD_CONTEXT_ORIONLDCONTEXTFROMTREE_H_
#define SRC_LIB_ORIONLD_CONTEXT_ORIONLDCONTEXTFROMTREE_H_

/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext, OrionldContextOrigin



// -----------------------------------------------------------------------------
//
// orionldContextFromTree -
//
// 'ephemeral' is for callers that resolve an inline @context over and over (once per entity of a
// batch) and that keep the resulting context no longer than the current request. It puts the
// context in the calling thread's own arena instead of the process-global one - see
// orionldContextCreate. Only contexts without a URL can be ephemeral; contexts that end up in the
// context cache ignore the flag.
//
extern OrionldContext* orionldContextFromTree(char* url, OrionldContextOrigin origin, char* id, KjNode* contextTreeP, bool ephemeral = false);

#endif  // SRC_LIB_ORIONLD_CONTEXT_ORIONLDCONTEXTFROMTREE_H_
