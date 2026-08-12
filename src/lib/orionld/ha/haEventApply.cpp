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
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "common/sem.h"                                          // cacheSemTake, cacheSemGive

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/subCache/subCacheItemFromDb.h"                 // subCacheItemFromDb
#include "orionld/subCache/subCacheItemRemove.h"                 // subCacheItemRemove
#include "orionld/regCache/regCacheItemFromDb.h"                 // regCacheItemFromDb
#include "orionld/regCache/regCacheItemRemove.h"                 // regCacheItemRemove
#include "orionld/ha/haEventApply.h"                             // Own interface



// -----------------------------------------------------------------------------
//
// subscriptionApply -
//
// The cached item is rebuilt by subCacheItemFromDb - the same function the NGSIv2
// write paths use, which reads the subscription back and runs it through
// dbModelToApiSubscription. Going through it, and not doing the conversion here,
// is what keeps a subscription that arrived over HA identical to one created
// locally. It also inherits, for free, the three things it got right the hard way:
// the payload checks it re-runs cannot rewrite an HTTP response, a periodic
// subscription is left to its own cache, and an @context is looked up, never
// downloaded.
//
// ⚠️ The counters are NOT clobbered: subCacheItemUpdate keeps 'deltas', the
// notifications this instance has sent and not yet flushed. An HA event says what
// the subscription IS, not how often we have notified about it.
//
static bool subscriptionApply(HaEvent* eventP)
{
  if (eventP->op == HaOpDelete)
  {
    bool removed = subCacheItemRemove(eventP->tenantP->subCache, eventP->id);

    KT_T(KtSubCache, "HA: subscription '%s' %s", eventP->id, removed? "removed from the cache" : "was not cached");
    return true;
  }

  //
  // The mongo channel hands over no payload - it shares the database the event
  // came from, so the document is read here. A channel that DOES carry the
  // subscription (haaux) will hand it over in 'apiP' and this read goes away.
  //
  if (eventP->apiP != NULL)
    KT_RE(false, "HA: subscription '%s' came with a payload - not implemented yet (haaux)", eventP->id);

  return subCacheItemFromDb(eventP->tenantP, eventP->id);
}



// -----------------------------------------------------------------------------
//
// registrationApply -
//
// Same shape as the subscription: the item is rebuilt by the very function the
// startup population uses (regCacheItemFromDbTree, through regCacheItemFromDb),
// so a registration that arrived over HA is cached exactly like one read at boot.
//
static bool registrationApply(HaEvent* eventP)
{
  if (eventP->op == HaOpDelete)
  {
    bool removed = regCacheItemRemove(eventP->tenantP->regCache, eventP->id);

    KT_T(KtRegCache, "HA: registration '%s' %s", eventP->id, removed? "removed from the cache" : "was not cached");
    return true;
  }

  if (eventP->apiP != NULL)
    KT_RE(false, "HA: registration '%s' came with a payload - not implemented yet (haaux)", eventP->id);

  return regCacheItemFromDb(eventP->tenantP, eventP->id);
}



// -----------------------------------------------------------------------------
//
// haEventApply -
//
bool haEventApply(HaEvent* eventP)
{
  if ((eventP->tenantP == NULL) || (eventP->id == NULL))
    KT_RE(false, "HA: event without a tenant or an id - ignored");

  KT_T(KtSubCache, "HA: applying %s of %s '%s' (tenant '%s')",
       (eventP->op == HaOpDelete)? "a delete" : "an upsert",
       (eventP->kind == HaSubscription)? "subscription" : (eventP->kind == HaRegistration)? "registration" : "@context",
       eventP->id,
       eventP->tenantP->tenant);

  //
  // ⚠️ The apply runs AS the event's tenant.
  //
  // There is no request behind this thread, so orionldStateInit left
  // orionldState.tenantP NULL - and the functions that read the database from
  // here (mongocSubscriptionLookup and everything else going through
  // mongocConnectionGet) take the tenant from orionldState, not as a parameter.
  // Without this the collection is opened on a NULL database name and the broker
  // segfaults on the very first event.
  //
  orionldState.tenantP = eventP->tenantP;

  bool ok = true;

  cacheSemTake(__FUNCTION__, "Applying an HA event");

  switch (eventP->kind)
  {
  case HaSubscription:
    ok = subscriptionApply(eventP);
    break;

  case HaRegistration:
    ok = registrationApply(eventP);
    break;

  case HaContext:
    //
    // TODO: the @context cache. Less urgent - a context is immutable once created
    // and a miss is resolved by reading the database anyway.
    //
    KT_T(KtSubCache, "HA: @context '%s' changed in another instance - not applied (not implemented)", eventP->id);
    break;
  }

  cacheSemGive(__FUNCTION__, "Applying an HA event");

  return ok;
}
