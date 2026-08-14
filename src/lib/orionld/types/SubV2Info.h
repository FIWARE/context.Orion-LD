#ifndef SRC_LIB_ORIONLD_TYPES_SUBV2INFO_H_
#define SRC_LIB_ORIONLD_TYPES_SUBV2INFO_H_

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
#include <string>                                                // std::string
#include <vector>                                                // std::vector

#include "apiTypesV2/HttpInfo.h"                                 // HttpInfo
#include "apiTypesV2/SubscriptionExpression.h"                   // SubscriptionExpression



// -----------------------------------------------------------------------------
//
// SubV2Info - everything a Subscription needs that ONLY NGSIv2 has
//
// A SubCacheItem is C - no std::string, no std::vector - exactly like the reg
// cache. NGSIv2 cannot be served that way: its matching needs a compiled
// StringFilter, an ngsiv2::HttpInfo with the custom-notification machinery, and
// a couple of concepts NGSI-LD simply does not have (servicePath, blacklist,
// attribute metadata).
//
// Rather than let that back into SubCacheItem, it lives out here and hangs off
// the item by pointer, allocated ONLY for an NGSIv2 subscription
// (SubCacheItem::ngsild == false) - the same shape as MqttInfo* mqttP, which is
// allocated only for an MQTT endpoint. One cache, one list to walk, and the C++
// stays in the v2 corner where it belongs.
//
// NOTE
//   NGSIv2's 'q' and NGSI-LD's 'q' are similar but NOT compatible, so this
//   deliberately keeps its own compiled filter. SubCacheItem::qP (a QNode tree
//   built from "ldQ") is the NGSI-LD one and the two never mix.
//
typedef struct SubV2Info
{
  SubscriptionExpression    expression;    // q, mq, geometry, coords, georel + the compiled StringFilters
  ngsiv2::HttpInfo          httpInfo;      // NGSIv2 custom notifications: method, payload template, qs, headers
  std::vector<std::string>  attributes;    // Notified attributes, in the form the v2 sender wants them
  std::vector<std::string>  metadata;      // Attribute metadata to include - no NGSI-LD equivalent
  bool                      blacklist;     // 'attributes' is an exclude-list, not an include-list
  char*                     servicePath;   // NGSIv2 service path - no NGSI-LD equivalent
} SubV2Info;

#endif  // SRC_LIB_ORIONLD_TYPES_SUBV2INFO_H_
