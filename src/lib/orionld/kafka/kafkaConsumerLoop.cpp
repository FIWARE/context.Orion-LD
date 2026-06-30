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
#include <sys/time.h>                                          // gettimeofday
#include <librdkafka/rdkafka.h>                                // rd_kafka_*

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjArray, kjChildAdd
}

#include "orionld/common/traceLevels.h"                        // StKafka
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/kafka/kafkaMessageParse.h"                   // kafkaMessageParse
#include "orionld/kafka/kafkaBatchProcess.h"                   // kafkaBatchProcess
#include "rest/mhd.h"                                          // MHD_Connection, MHD_RequestTerminationCode
#include "orionld/kafka/kafkaConsumerLoop.h"                   // Own interface


// -----------------------------------------------------------------------------
//
// requestCompleted - defined in rest/rest.cpp, used for thread-local state cleanup
//
extern void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);



// -----------------------------------------------------------------------------
//
// External references
//
extern volatile bool  kafkaRunning;
extern rd_kafka_t*    kafkaConsumerHandle;
extern int            kafkaBatchSize;
extern int            kafkaBatchLingerMs;



// -----------------------------------------------------------------------------
//
// currentTimeMs - get current time in milliseconds
//
static double currentTimeMs(void)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}



// -----------------------------------------------------------------------------
//
// kafkaConsumerLoop - main loop for a Kafka consumer thread
//
// Polls messages from Kafka, accumulates them into micro-batches,
// and processes each batch through the NGSI-LD batch upsert pipeline.
//
void* kafkaConsumerLoop(void* vP)
{
  KT_I("Kafka consumer loop started");

  while (kafkaRunning)
  {
    //
    // Initialize thread-local orionldState for this batch cycle.
    // This gives us a fresh kjson parser and kalloc arena.
    //
    orionldStateInit(NULL);

    KjNode*  entityArray     = kjArray(orionldState.kjsonP, NULL);
    int      entityCount     = 0;
    double   batchStartTime  = 0;
    bool     batchStarted    = false;

    //
    // Accumulate messages into a micro-batch
    //
    while (kafkaRunning)
    {
      int pollTimeoutMs = batchStarted ? kafkaBatchLingerMs : 100;

      rd_kafka_message_t* msg = rd_kafka_consumer_poll(kafkaConsumerHandle, pollTimeoutMs);

      if (msg != NULL)
      {
        if (msg->err == RD_KAFKA_RESP_ERR_NO_ERROR)
        {
          KT_T(KtKafka, "Received Kafka message (%d bytes) from partition %d, offset %ld",
               (int) msg->len, msg->partition, (long) msg->offset);
          KT_T(KtKafkaDetail, "Kafka message payload: %.*s", (int) msg->len, (const char*) msg->payload);

          KjNode* parsed = kafkaMessageParse(orionldState.kjsonP,
                                             (const char*) msg->payload,
                                             (int) msg->len);
          if (parsed != NULL)
          {
            if (parsed->type == KjArray)
            {
              // Batch message: move all children into our entityArray
              KjNode* next;
              int arrayCount = 0;
              for (KjNode* entityP = parsed->value.firstChildP; entityP != NULL; entityP = next)
              {
                next = entityP->next;
                entityP->next = NULL;
                kjChildAdd(entityArray, entityP);
                entityCount++;
                arrayCount++;
              }
              KT_T(KtKafka, "Parsed %d entities from Kafka batch message", arrayCount);
            }
            else
            {
              // Single entity
              kjChildAdd(entityArray, parsed);
              entityCount++;
              KT_T(KtKafka, "Parsed 1 entity from Kafka message");
            }

            if (!batchStarted)
            {
              batchStartTime = currentTimeMs();
              batchStarted   = true;
            }
          }
          else
          {
            KT_W("Kafka message discarded (parse failed), %d bytes from partition %d, offset %ld",
                 (int) msg->len, msg->partition, (long) msg->offset);
          }
        }
        else if (msg->err != RD_KAFKA_RESP_ERR__PARTITION_EOF)
        {
          KT_W("Kafka consumer error: %s", rd_kafka_message_errstr(msg));
        }

        rd_kafka_message_destroy(msg);
      }

      //
      // Check flush triggers
      //
      bool shouldFlush = false;

      if (entityCount >= kafkaBatchSize)
        shouldFlush = true;
      else if (batchStarted && (currentTimeMs() - batchStartTime >= kafkaBatchLingerMs))
        shouldFlush = true;
      else if (msg == NULL && entityCount > 0)
        shouldFlush = true;  // Empty poll with pending entities

      if (shouldFlush)
        break;
    }

    //
    // Process the batch if we have entities.
    //
    // NOTE: kafkaBatchProcess() only performs the MongoDB (current state) upsert. The TRoE
    // (temporal/Postgres) write is done later, by requestCompleted() below, via
    // orionldState.serviceP->troeRoutine(). So 'mongoOk' reflects MongoDB durability only.
    //
    bool mongoOk = false;

    if (entityCount > 0)
    {
      KT_T(KtKafka, "Processing Kafka batch of %d entities", entityCount);

      mongoOk = kafkaBatchProcess(entityArray);

      if (mongoOk)
        KT_T(KtKafka, "Kafka batch of %d entities written to MongoDB", entityCount);
      else
        KT_W("Kafka batch processing failed (MongoDB) for %d entities, offsets not committed", entityCount);
    }

    //
    // Cleanup thread-local state (frees kalloc arena, etc.).
    //
    // IMPORTANT: requestCompleted() also performs the TRoE (temporal/Postgres) write. It must
    // run BEFORE the Kafka offset is committed, so that a failed TRoE write prevents the commit
    // and the batch gets redelivered. Committing the offset before the TRoE write (the previous
    // behaviour) meant a Postgres failure left MongoDB with a newer value than the temporal
    // history, with no chance of recovery. pgCommands() flags such failures in
    // orionldState.troeError, which we check below before committing.
    //
    void* con_cls = NULL;
    requestCompleted(NULL, NULL, &con_cls, MHD_REQUEST_TERMINATED_COMPLETED_OK);

    //
    // Commit offsets only after BOTH the MongoDB upsert AND the TRoE write succeeded.
    //
    if (entityCount > 0)
    {
      if (mongoOk && (orionldState.troeError == false))
      {
        // RD_KAFKA_RESP_ERR__NO_OFFSET is benign: another consumer thread already committed
        // the stored offsets via the shared kafkaConsumerHandle.
        rd_kafka_resp_err_t err = rd_kafka_commit(kafkaConsumerHandle, NULL, 0);
        if (err == RD_KAFKA_RESP_ERR_NO_ERROR)
          KT_T(KtKafka, "Kafka offsets committed (%d entities durable in MongoDB and TRoE)", entityCount);
        else if (err == RD_KAFKA_RESP_ERR__NO_OFFSET)
          KT_T(KtKafka, "Kafka offset commit skipped (already committed by another thread)");
        else
          KT_W("Kafka offset commit failed: %s", rd_kafka_err2str(err));
      }
      else if (mongoOk)  // MongoDB ok but TRoE write failed (orionldState.troeError == true)
        KT_W("Kafka batch of %d entities: TRoE write failed, offsets NOT committed (batch will be redelivered)", entityCount);
    }
  }

  KT_I("Kafka consumer loop exiting");
  return NULL;
}
