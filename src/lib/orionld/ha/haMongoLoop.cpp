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
#include <string.h>                                              // strcmp, strncmp, strlen
#include <unistd.h>                                              // sleep
#include <pthread.h>                                             // pthread_create, pthread_detach
#include <mongoc/mongoc.h>                                       // MongoDB C Client Driver

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaBufferReset.h"                                // kaBufferReset
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/OrionldTenant.h"                         // OrionldTenant, tenant0
#include "orionld/common/orionldState.h"                         // orionldState, mongocUri, dbName
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/common/orionldTenantGet.h"                     // orionldTenantGet
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocKjTreeFromBson.h"                 // mongocKjTreeFromBson
#include "orionld/ha/HaEvent.h"                                  // HaEvent
#include "orionld/ha/haEventApply.h"                             // haEventApply
#include "orionld/ha/haMongoLoop.h"                              // Own interface



// -----------------------------------------------------------------------------
//
// tenantOfDatabase - which tenant does this database belong to?
//
// The default tenant's database is 'dbName' itself; every other tenant's is
// "<dbName>-<tenant>" (orionldTenantCreate). Anything else in the deployment is
// not ours - the watch is cluster-wide, so other applications' databases come
// past too.
//
// A tenant this instance has never heard of is CREATED, caches and all: another
// instance inventing a tenant is exactly the kind of thing HA has to learn about.
//
static OrionldTenant* tenantOfDatabase(const char* db)
{
  int dbNameLen = strlen(dbName);

  if (strcmp(db, dbName) == 0)
    return &tenant0;

  if ((strncmp(db, dbName, dbNameLen) == 0) && (db[dbNameLen] == '-'))
    return orionldTenantGet(&db[dbNameLen + 1]);

  return NULL;
}



// -----------------------------------------------------------------------------
//
// kindOfCollection -
//
static bool kindOfCollection(const char* coll, HaKind* kindP)
{
  if      (strcmp(coll, "csubs")         == 0)  *kindP = HaSubscription;
  else if (strcmp(coll, "registrations") == 0)  *kindP = HaRegistration;
  else if (strcmp(coll, "contexts")      == 0)  *kindP = HaContext;
  else
    return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// eventTreat - turn one change-stream document into an HaEvent and apply it
//
static void eventTreat(const bson_t* bsonP)
{
  char*   title;
  char*   details;
  KjNode* eventP = mongocKjTreeFromBson(bsonP, &title, &details);

  if (eventP == NULL)
    KT_RVE("HA: unable to parse a change stream event (%s: %s)", title, details);

  KjNode* opTypeP = kjLookup(eventP, "operationType");
  KjNode* nsP     = kjLookup(eventP, "ns");

  if ((opTypeP == NULL) || (opTypeP->type != KjString) || (nsP == NULL))
    KT_RVE("HA: change stream event without operationType or ns - ignored");

  //
  // 'invalidate', 'drop', 'dropDatabase', 'rename' - the collection or database
  // itself went away. Nothing sensible to do per item; the caches are rebuilt at
  // the next start. Worth a warning though, it is not a normal thing to happen.
  //
  HaOp op;

  if      (strcmp(opTypeP->value.s, "insert")  == 0) op = HaOpUpsert;
  else if (strcmp(opTypeP->value.s, "update")  == 0) op = HaOpUpsert;
  else if (strcmp(opTypeP->value.s, "replace") == 0) op = HaOpUpsert;
  else if (strcmp(opTypeP->value.s, "delete")  == 0) op = HaOpDelete;
  else
  {
    KT_W("HA: change stream event '%s' - not an item change, ignored", opTypeP->value.s);
    return;
  }

  KjNode* dbP   = kjLookup(nsP, "db");
  KjNode* collP = kjLookup(nsP, "coll");

  if ((dbP == NULL) || (collP == NULL) || (dbP->type != KjString) || (collP->type != KjString))
    KT_RVE("HA: change stream event with an incomplete 'ns' - ignored");

  HaKind kind;

  if (kindOfCollection(collP->value.s, &kind) == false)
    return;  // Not one of ours - entities, and everybody else's collections

  //
  // Which tenant?
  //
  // Subscriptions and registrations live in the tenant's own database. @contexts
  // do NOT: the context cache is global (a context is identified by its URL, and
  // the document at a URL is the same whoever fetched it), and the collection sits
  // in a database called "orionld" whatever -db says - see mongocConnectionGet.
  // So a context event is accepted from there and from nowhere else, and the
  // tenant it carries is only there because the apply wants one.
  //
  OrionldTenant* tenantP;

  if (kind == HaContext)
  {
    if (strcmp(dbP->value.s, "orionld") != 0)
      return;  // A collection called "contexts" in somebody else's database

    tenantP = &tenant0;
  }
  else
  {
    tenantP = tenantOfDatabase(dbP->value.s);

    if (tenantP == NULL)
      return;  // Another application's database
  }

  //
  // The id. For a Subscription or a Registration created over NGSI-LD it is the
  // URI; an NGSIv2 subscription has a mongo OID instead, which this instance has
  // no way to act on through the NGSI-LD caches.
  //
  KjNode* documentKeyP = kjLookup(eventP, "documentKey");
  KjNode* idP          = (documentKeyP != NULL)? kjLookup(documentKeyP, "_id") : NULL;

  if ((idP == NULL) || (idP->type != KjString))
  {
    KT_T(KtSubCache, "HA: change in %s.%s with a non-string _id (an NGSIv2 subscription?) - ignored",
         dbP->value.s, collP->value.s);
    return;
  }

  HaEvent haEvent;

  haEvent.op      = op;
  haEvent.kind    = kind;
  haEvent.tenantP = tenantP;
  haEvent.id      = idP->value.s;
  haEvent.apiP    = NULL;  // mongo channel: the document is read by the apply step

  haEventApply(&haEvent);
}



// -----------------------------------------------------------------------------
//
// haMongoLoopThread - one thread, one watch, every tenant and all three collections
//
// mongoc_client_watch() watches the whole deployment, so a single cursor covers
// every tenant's database - a tenant is a database, and there is no telling in
// advance which ones exist. The alternative, a watch per database, would mean a
// thread per tenant and no way to notice a tenant that appears later.
//
// mongoc_change_stream_next() BLOCKS until something happens, so this thread
// costs nothing while the deployment is quiet. It is not a poll.
//
static void* haMongoLoopThread(void* vP)
{
  //
  // A client of its own, not one from the pool: this one is held for the lifetime
  // of the broker, and taking a pool slot away from the request threads forever
  // would be a poor trade for a thread that spends its life blocked.
  //
  mongoc_client_t* clientP = mongoc_client_new_from_uri(mongocUri);

  if (clientP == NULL)
    KT_X(1, "HA: unable to create a mongo client for the change stream");

  bson_t pipeline = BSON_INITIALIZER;   // Empty: everything. The filtering is done above, in C

  //
  // maxAwaitTimeMS is how long the server holds the cursor open waiting for
  // something to happen. Without it the driver's default is used and next()
  // returns constantly - this is the difference between a blocked thread and a
  // spinning one.
  //
  bson_t* optsP = BCON_NEW("maxAwaitTimeMS", BCON_INT32(1000));

  while (1)
  {
    mongoc_change_stream_t* streamP = mongoc_client_watch(clientP, &pipeline, optsP);
    const bson_t*           bsonP;
    bson_error_t            error;
    const bson_t*           reply;

    KT_T(KtSubCache, "HA: watching the database for changes made by other broker instances");

    //
    // ⚠️ next() returning false does NOT mean the stream is finished - it means
    // "nothing right now", every maxAwaitTimeMS, for as long as the deployment is
    // quiet. Only error_document() tells the two apart, and treating a quiet
    // moment as a broken stream tears the watch down and rebuilds it once a
    // second, forever, without ever delivering an event.
    //
    while (true)
    {
      if (mongoc_change_stream_next(streamP, &bsonP) == true)
      {
        //
        // A thread with no request behind it - it needs an orionldState of its
        // own, and the kalloc buffer it uses is given back after every event.
        //
        orionldStateInit(NULL);
        eventTreat(bsonP);
        kaBufferReset(&orionldState.kalloc, true);
        orionldStateRelease();
        continue;
      }

      if (mongoc_change_stream_error_document(streamP, &error, &reply) == true)
        break;  // A real error - out to have the stream recreated
    }

    //
    // mongoc resumes by itself over a transient error, so getting here means it
    // could not. Anything missed in the meantime is picked up by a restart, and
    // by whatever -subCacheIval is still doing.
    //
    KT_E("HA: change stream error (%s) - restarting the stream in 5 seconds", error.message);

    mongoc_change_stream_destroy(streamP);
    sleep(5);
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// haMongoLoopStart -
//
bool haMongoLoopStart(void)
{
  pthread_t  tid;
  int        ret;

  ret = pthread_create(&tid, NULL, haMongoLoopThread, NULL);

  if (ret != 0)
    KT_RE(false, "HA: unable to create the change stream thread (%d)", ret);

  pthread_detach(tid);

  return true;
}
