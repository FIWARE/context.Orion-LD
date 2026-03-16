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
*/
#include <pthread.h>                                           // pthread_create
#include <stdlib.h>                                            // calloc
#include <librdkafka/rdkafka.h>                                // rd_kafka_*

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/kafka/kafkaConsumerLoop.h"                   // kafkaConsumerLoop
#include "orionld/kafka/kafkaInit.h"                           // Own interface



// -----------------------------------------------------------------------------
//
// Globals for Kafka consumer subsystem
//
volatile bool   kafkaRunning         = true;
rd_kafka_t*     kafkaConsumerHandle  = NULL;
pthread_t*      kafkaThreadIds       = NULL;

// These are set from orionld.cpp CLI options
extern char     kafkaBrokerList[];
extern char     kafkaTopic[];
extern char     kafkaGroupId[];
extern int      kafkaBatchSize;
extern int      kafkaBatchLingerMs;
extern int      kafkaConsumerThreads;



// -----------------------------------------------------------------------------
//
// kafkaInit - initialize the Kafka consumer and start consumer threads
//
bool kafkaInit(void)
{
  char            errstr[512];
  rd_kafka_conf_t*                   conf;
  rd_kafka_topic_partition_list_t*   topics;

  KT_I("Initializing Kafka consumer (brokers: %s, topic: %s, group: %s, threads: %d)",
       kafkaBrokerList, kafkaTopic, kafkaGroupId, kafkaConsumerThreads);

  //
  // Create Kafka configuration
  //
  conf = rd_kafka_conf_new();
  if (conf == NULL)
  {
    KT_E("kafkaInit: failed to create Kafka configuration");
    return false;
  }

  if (rd_kafka_conf_set(conf, "bootstrap.servers", kafkaBrokerList, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
  {
    KT_E("kafkaInit: failed to set bootstrap.servers: %s", errstr);
    rd_kafka_conf_destroy(conf);
    return false;
  }

  if (rd_kafka_conf_set(conf, "group.id", kafkaGroupId, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
  {
    KT_E("kafkaInit: failed to set group.id: %s", errstr);
    rd_kafka_conf_destroy(conf);
    return false;
  }

  if (rd_kafka_conf_set(conf, "auto.offset.reset", "latest", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
  {
    KT_E("kafkaInit: failed to set auto.offset.reset: %s", errstr);
    rd_kafka_conf_destroy(conf);
    return false;
  }

  // Disable auto-commit; we commit manually after successful processing
  if (rd_kafka_conf_set(conf, "enable.auto.commit", "false", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
  {
    KT_E("kafkaInit: failed to set enable.auto.commit: %s", errstr);
    rd_kafka_conf_destroy(conf);
    return false;
  }

  //
  // Create Kafka consumer
  //
  kafkaConsumerHandle = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
  if (kafkaConsumerHandle == NULL)
  {
    KT_E("kafkaInit: failed to create Kafka consumer: %s", errstr);
    // conf is destroyed by rd_kafka_new on failure
    return false;
  }

  // Redirect all partitions to the consumer queue (required for high-level consumer)
  rd_kafka_poll_set_consumer(kafkaConsumerHandle);

  //
  // Subscribe to topic
  //
  topics = rd_kafka_topic_partition_list_new(1);
  rd_kafka_topic_partition_list_add(topics, kafkaTopic, RD_KAFKA_PARTITION_UA);

  rd_kafka_resp_err_t err = rd_kafka_subscribe(kafkaConsumerHandle, topics);
  rd_kafka_topic_partition_list_destroy(topics);

  if (err != RD_KAFKA_RESP_ERR_NO_ERROR)
  {
    KT_E("kafkaInit: failed to subscribe to topic '%s': %s", kafkaTopic, rd_kafka_err2str(err));
    rd_kafka_destroy(kafkaConsumerHandle);
    kafkaConsumerHandle = NULL;
    return false;
  }

  //
  // Start consumer threads
  //
  kafkaThreadIds = (pthread_t*) calloc(kafkaConsumerThreads, sizeof(pthread_t));
  if (kafkaThreadIds == NULL)
  {
    KT_E("kafkaInit: out of memory allocating thread IDs");
    rd_kafka_destroy(kafkaConsumerHandle);
    kafkaConsumerHandle = NULL;
    return false;
  }

  for (int ix = 0; ix < kafkaConsumerThreads; ix++)
  {
    int r = pthread_create(&kafkaThreadIds[ix], NULL, kafkaConsumerLoop, NULL);
    if (r != 0)
    {
      KT_E("kafkaInit: failed to create consumer thread %d: error %d", ix, r);
      kafkaRunning = false;
      return false;
    }
    KT_I("Kafka consumer thread %d started", ix);
  }

  KT_I("Kafka consumer initialized successfully");
  return true;
}
