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
extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/subCache/apiModelToCacheSubscription.h"        // Own interface



// -----------------------------------------------------------------------------
//
// apiModelToCacheSubscription -
//
// Adapts an API-model Subscription into the shape the cache wants, in place,
// right before subCacheItemAdd clones it into the cache item.
//
// Nothing to adapt yet - the tree goes into the cache as it comes out of the API
// model. This is the hook where the *compiled* matching state will be built, so
// that matching an entity update never has to parse anything:
//
//   - 'q'          -> parsed QNode tree
//   - 'geoQ'       -> OrionldGeoInfo + a prepared GEOS geometry
//   - entity ids   -> compiled idPattern regexes
//   - notification -> trigger bitmask, render format, endpoint split into
//                     protocol/ip/port/rest
//
// Those live in CachedSubscription today and move here as the new cache takes
// over. Keeping the hook in place from the start means the call sites do not
// change again when they do.
//
void apiModelToCacheSubscription(KjNode* apiSubscriptionP)
{
  // Intentionally empty for now - see the comment above.
}
