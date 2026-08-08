#ifndef SRC_LIB_ORIONLD_TYPES_SUBCACHEITEM_H_
#define SRC_LIB_ORIONLD_TYPES_SUBCACHEITEM_H_

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
#include <stdint.h>                                              // types: uint32_t, ...
#include <geos_c.h>                                              // GEOSGeometry, GEOSPreparedGeometry

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/types/OrionldGeoInfo.h"                        // OrionldGeoInfo
#include "orionld/types/OrionldMimeType.h"                       // MimeType
#include "orionld/types/MqttInfo.h"                              // MqttInfo
#include "orionld/types/OrionldRenderFormat.h"                   // OrionldRenderFormat
#include "orionld/types/Protocol.h"                              // Protocol
#include "orionld/types/QNode.h"                                 // QNode
#include "orionld/types/SubEntitySelector.h"                     // SubEntitySelector



// -----------------------------------------------------------------------------
//
// SUB_TRIGGER - bit in SubCacheItem::triggers for an OrionldAlterationType
//
// The alteration types start at 1, so bit 0 is unused and SUB_TRIGGERS_ALL
// covers bits 1 .. OrionldAlterationTypes.
//
#define SUB_TRIGGER(altType)  (1 << (altType))
#define SUB_TRIGGERS_ALL      0xFFFFFFFE



// -----------------------------------------------------------------------------
//
// SubDeltas - notification counters not yet flushed to the database
//
// Only the counters - they are what the flush $inc's. The timestamps that go
// with them are absolute, not deltas, and live in the SubCacheItem itself.
//
// 'timesSent' counts every notification ATTEMPT, successful or not (that is what
// the API renders), so it doubles as "counter updates since the last flush".
//
typedef struct SubDeltas
{
  uint32_t timesSent;
  uint32_t timesFailed;
} SubDeltas;



// -----------------------------------------------------------------------------
//
// SubCacheItem -
//
typedef struct SubCacheItem
{
  char*                 subId;              // Set when creating subscription - points inside subTree
  KjNode*               subTree;
  bool                  ngsild;             // false for an NGSIv2 subscription, whose database _id is an OID, not a string
  bool                  dirty;              // The subscription has been patched - not only counters differ from copy in DB
  OrionldContext*       contextP;           // Set when creating/patching registration
  char*                 hostAlias;          // Broker identity - for the Via header

  //
  // Notification bookkeeping. The counters in the subTree are what the database
  // holds; 'deltas' is what has happened since and is not yet flushed. What the
  // API renders is the sum. The timestamps are absolute and always the latest.
  //
  SubDeltas             deltas;
  double                lastNotificationTime;  // Last notification ATTEMPT - seeded from the DB, read by throttling
  double                lastSuccess;
  double                lastFailure;
  int                   consecutiveErrors;     // Three in a row and the subscription is paused. Not in the DB
  char                  lastErrorReason[128];  // Not in the DB

  //
  // "Shortcuts" and compiled state - all of it built from subTree, at cache time,
  // so that matching an entity alteration never has to parse anything.
  //
  // Only what needs TRANSFORMING (a regex, a QNode tree, a GEOS geometry, an
  // enum, a split URL) or what is read on every single alteration lives here.
  // Everything else - name, description, watchedAttributes, notified attributes,
  // datasetId, the counters, ... - is read from the subTree, which is the source
  // of truth.
  //
  double                      modifiedAt;      // Copied from inside the subTree
  bool                        isActive;
  double                      expiresAt;       // 0: never expires
  double                      throttling;      // 0: no throttling
  char*                       lang;            // Points inside subTree
  uint32_t                    triggers;        // Bitmask of SUB_TRIGGER(OrionldAlterationType)
  OrionldRenderFormat         renderFormat;
  bool                        showChanges;
  bool                        sysAttrs;

  SubEntitySelector*          entitySelectors; // The "entities" array, compiled (idPattern -> regex)

  char*                       qText;           // Points inside subTree - the expanded 'q' (DB name: "ldQ")
  QNode*                      qP;              // 'qText', parsed

  KjNode*                     geoQP;           // Clone of "geoQ" - owns the memory 'geoInfo' points into
  OrionldGeoInfo*             geoInfo;
  GEOSGeometry*               geosGeometry;
  const GEOSPreparedGeometry* geosPrepared;

  // notification::endpoint::uri, split up
  char*                       url;             // Own copy - urlParse destroys its input
  char*                       protocolString;  // Points inside 'url' - or at the literal "none" if the URI has no scheme
  char*                       ip;              // Points inside 'url'
  uint16_t                    port;
  char*                       rest;            // Points inside 'url'
  Protocol                    protocol;
  MimeType                    mimeType;        // notification::endpoint::accept
  MqttInfo*                   mqttP;           // Only for an MQTT/MQTTS endpoint - the URI split up MQTT-style, + notifierInfo

  struct SubCacheItem*  next;
} SubCacheItem;

#endif  // SRC_LIB_ORIONLD_TYPES_SUBCACHEITEM_H_
