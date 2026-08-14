#ifndef SRC_LIB_ORIONLD_HA_HAEVENT_H_
#define SRC_LIB_ORIONLD_HA_HAEVENT_H_

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

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant



// -----------------------------------------------------------------------------
//
// HaOp - what happened to it
//
// An upsert covers create AND update on purpose: applying either means the same
// thing to a cache - make my copy match what the other instance has now - and the
// receiving broker cannot know whether it already had the item anyway.
//
typedef enum HaOp
{
  HaOpUpsert,
  HaOpDelete
} HaOp;



// -----------------------------------------------------------------------------
//
// HaKind - what it is
//
typedef enum HaKind
{
  HaSubscription,
  HaRegistration,
  HaContext
} HaKind;



// -----------------------------------------------------------------------------
//
// HaEvent - "another instance did this", independent of how we were told
//
// The point of this type is that haEventApply() never learns which channel the
// event arrived on. Today there is one (mongo change streams); haaux, over a
// socket, is meant to be the other, and for a deployment without mongo it will be
// the only one. Two channels feeding two apply paths would drift, and silently -
// that is the lesson the whole sub-cache migration kept teaching.
//
// 'apiP' is the NGSI-LD API representation of the thing - never a database model.
// That matters for haaux, whose wire format has to be something Scorpio or Stellio
// could also implement; it is NOT the NGSIv1-shaped document orion-ld happens to
// store. The mongo channel leaves it NULL: it has no wire, it shares the database
// the event came from, so it lets the apply step read the document itself through
// the very same function every other write path uses.
//
typedef struct HaEvent
{
  HaOp            op;
  HaKind          kind;
  OrionldTenant*  tenantP;
  const char*     id;
  KjNode*         apiP;      // NULL: read it from the database (mongo channel)
} HaEvent;

#endif  // SRC_LIB_ORIONLD_HA_HAEVENT_H_
