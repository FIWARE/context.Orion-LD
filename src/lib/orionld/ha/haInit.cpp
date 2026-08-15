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
#include <string.h>                                              // strcmp
#include <unistd.h>                                              // usleep
#include <mongoc/mongoc.h>                                       // MongoDB C Client Driver

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/orionldState.h"                         // orionldState, haChannel, mongocPool
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ha/haMongoLoop.h"                              // haMongoLoopStart
#include "orionld/ha/haInit.h"                                   // Own interface



// -----------------------------------------------------------------------------
//
// replicaSetCheck - is the mongod we are talking to part of a replica set?
//
// Change streams read the oplog, and a standalone mongod has none - it answers
// every watch with "The $changeStream stage is only supported on replica sets".
// Better to say so at startup than to run with a sync that silently never fires.
//
// The check ASKS THE SERVER rather than looking at -rplSet. A single-node replica
// set is reached perfectly well over a direct connection with no replicaSet= in
// the URI and no -rplSet at all - that is how the development setup works - so
// -rplSet would have refused a deployment that is in fact perfectly able to run.
//
static bool replicaSetCheck(void)
{
  mongoc_client_t* clientP = mongoc_client_pool_pop(mongocPool);

  if (clientP == NULL)
    KT_RE(false, "HA: no mongo connection to check the deployment with");

  bson_t*       command = BCON_NEW("isMaster", BCON_INT32(1));
  bson_t        reply;
  bson_error_t  error;
  bool          isReplicaSet = false;

  if (mongoc_client_command_simple(clientP, "admin", command, NULL, &reply, &error) == true)
  {
    bson_iter_t iter;

    // A replica set member names its set; a standalone has no 'setName'
    if (bson_iter_init_find(&iter, &reply, "setName") == true)
      isReplicaSet = true;
  }
  else
    KT_E("HA: unable to ask mongo whether it is a replica set (%s)", error.message);

  bson_destroy(&reply);
  bson_destroy(command);
  mongoc_client_pool_push(mongocPool, clientP);

  return isReplicaSet;
}



// -----------------------------------------------------------------------------
//
// haApplyEnabled - are the caches loaded?
//
// The channel is started BEFORE the caches are loaded, on purpose (see haInit.h),
// so an event can arrive before there is anything to apply it to. It waits here.
//
// A condition variable would do the same thing with more machinery: this is
// waited on once per broker start, by one thread, for a few milliseconds at
// most. 'volatile' is what makes the poll actually re-read it.
//
static volatile bool haApplyEnabled = false;



// -----------------------------------------------------------------------------
//
// haApplyEnable -
//
void haApplyEnable(void)
{
  haApplyEnabled = true;
}



// -----------------------------------------------------------------------------
//
// haApplyWait -
//
void haApplyWait(void)
{
  while (haApplyEnabled == false)
    usleep(10000);
}



// -----------------------------------------------------------------------------
//
// haInit -
//
bool haInit(void)
{
  if (haChannel[0] == 0)
  {
    KT_T(KtSubCache, "HA is off (no -ha)");
    return true;
  }

  if (strcmp(haChannel, "mongo") == 0)
  {
    if (replicaSetCheck() == false)
      KT_X(1, "-ha mongo needs a mongo REPLICA SET - this mongod is a standalone, and a standalone has no oplog for a change stream to read. "
              "Either point the broker at a replica set (a single node is enough: mongod --replSet <name>, then rs.initiate()) or run without -ha");

    KT_T(KtSubCache, "HA: using mongo change streams as the channel");
    return haMongoLoopStart();
  }

  //
  // Anything else is an address: the haaux HA Sync Auxiliary, which the broker
  // connects to over a socket. It is the channel for a deployment where mongo is
  // not the store (or not there at all), and it is not written yet - so say so
  // rather than start up pretending HA is on.
  //
  KT_X(1, "-ha %s: an address means the haaux HA channel, which is not implemented yet. Use '-ha mongo' (needs a replica set)", haChannel);

  return false;
}
