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
* Author: Carsten Frey
*/
#include <cstdlib>                                             // free
#include <pthread.h>                                           // pthread_join, pthread_t
#include <librdkafka/rdkafka.h>                                // rd_kafka_*

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/kafka/kafkaAckProducer.h"                    // kafkaAckProducerRelease
#include "orionld/kafka/kafkaRelease.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// External references
//
extern volatile bool  kafkaRunning;
extern rd_kafka_t*    kafkaConsumerHandle;
extern int            kafkaConsumerThreads;
extern pthread_t*     kafkaThreadIds;



// -----------------------------------------------------------------------------
//
// kafkaRelease -
//
void kafkaRelease(void)
{
  kafkaRunning = false;

  if (kafkaConsumerHandle == NULL)
    return;

  KT_I("Shutting down Kafka consumer");

  // Wait for consumer threads to finish
  for (int ix = 0; ix < kafkaConsumerThreads; ix++)
  {
    if (kafkaThreadIds[ix] != 0)
      pthread_join(kafkaThreadIds[ix], NULL);
  }

  rd_kafka_consumer_close(kafkaConsumerHandle);
  rd_kafka_destroy(kafkaConsumerHandle);
  kafkaConsumerHandle = NULL;

  if (kafkaThreadIds != NULL)
  {
    free(kafkaThreadIds);
    kafkaThreadIds = NULL;
  }

  // Flush + close the ACK/NACK feedback producer (no-op if it was never enabled).
  kafkaAckProducerRelease();

  KT_I("Kafka consumer shut down");
}
