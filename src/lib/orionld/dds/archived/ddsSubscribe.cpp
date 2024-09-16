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
* Author: David Campo, Ken Zangelin
*/
#include <string.h>
#include <pthread.h>                                        // pthread_exit

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
}

#include "orionld/dds/NgsildSubscriber.h"
#include "orionld/dds/DdsNotificationReceiver.h"            // DdsNotificationFunction



// -----------------------------------------------------------------------------
//
// SubscriberParams -
//
typedef struct SubscriberParams
{
  char*                    topicType;
  char*                    topicName;
  DdsNotificationFunction  callback;
} SubscriberParams;



// -----------------------------------------------------------------------------
//
// ddsSubscribe -
//
// EPROS: We would like to have one single subscriber, that subscribes to all DDS notifications
//        Obviously, we'd need a way to add topics to that subscriber "on the fly"
//
static void* ddsSubscribe2(void* vP)
{
  SubscriberParams* spP = (SubscriberParams*) vP;

  ktVerbose = KTRUE;
  KT_V("Creating a subscription on '%s/%s'", spP->topicType, spP->topicName);

  NgsildSubscriber* subP = new NgsildSubscriber(spP->topicType, spP->callback);

  if (subP->init(spP->topicName))
  {
    subP->run();
  }

  KT_V("Deleting the subscription on '%s/%s'", spP->topicType, spP->topicName);
  delete subP;

  return NULL;
}



// -----------------------------------------------------------------------------
//
// ddsSubscribe -
//
void ddsSubscribe(const char* topicType, const char* topicName, DdsNotificationFunction callback)
{
  pthread_t          tid;
  SubscriberParams*  spP = (SubscriberParams*) malloc(sizeof(SubscriberParams));

  spP->topicType = strdup(topicType);
  spP->topicName = strdup(topicName);
  spP->callback  = callback;

  KT_V("Starting thread for DDS subscription on %s/%s", spP->topicType, spP->topicName);
  pthread_create(&tid, NULL, ddsSubscribe2, spP);
}
