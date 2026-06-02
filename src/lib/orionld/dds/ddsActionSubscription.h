#ifndef SRC_LIB_ORIONLD_DDS_DDSACTIONSUBSCRIPTION_H_
#define SRC_LIB_ORIONLD_DDS_DDSACTIONSUBSCRIPTION_H_

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
// ddsActionSubscriptionCreate -
//
// Create a TEMPORARY subscription (subscription cache ONLY - never persisted to
// mongo) that watches the action-tied attribute on the goal's entity and
// notifies 'endpointUri' on every change (feedback / result / status). Used to
// stream a single goal's lifecycle to the client that initiated it.
//
// The subscription lives in tenant0 (where the per-goal datasetId instances are
// materialised - see ddsActionSubAttributeUpdate) so the alterations match.
//
// Must be called with orionldState initialised (it uses orionldState.kjsonP and
// orionldState.contextP for tree allocation and name expansion). Safe to call
// mid-PATCH: neither pCheckSubscription nor subCacheApiSubscriptionInsert writes
// the request's response state (httpStatusCode / responseTree / Location).
//
// Returns a libc-strdup'd copy of the new subscription id (caller frees), or
// NULL if endpointUri is NULL or creation failed (a failure here never fails
// the goal itself).
//
extern char* ddsActionSubscriptionCreate
(
  const char*  entityId,
  const char*  entityType,
  const char*  attributeName,
  const char*  endpointUri,
  const char*  datasetId     // the goal's "urn:goal:<uuid>" - projects notifications to this goal's instance
);



// -----------------------------------------------------------------------------
//
// ddsActionSubscriptionDelete -
//
// Remove a temporary action subscription from the subscription cache (tenant0).
// No-op if subscriptionId is NULL or the subscription is no longer in the cache.
//
extern void ddsActionSubscriptionDelete(const char* subscriptionId);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONSUBSCRIPTION_H_
