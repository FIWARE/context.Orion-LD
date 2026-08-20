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
#include <stdlib.h>                                            // malloc, free
#include <stdio.h>                                             // snprintf
#include <string.h>                                            // strstr
#include <librdkafka/rdkafka.h>                                // rd_kafka_*

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjArray, kjChildAdd
#include "kjson/kjRenderSize.h"                                // kjFastRenderSize
#include "kjson/kjRender.h"                                    // kjFastRender
}

#include "orionld/common/traceLevels.h"                        // StKafka
#include "orionld/common/orionldState.h"                       // orionldState, kafkaAckTopic
#include "orionld/kafka/kafkaMessageParse.h"                   // kafkaMessageParse
#include "orionld/kafka/kafkaBatchProcess.h"                   // kafkaBatchProcess
#include "orionld/kafka/kafkaAckProducer.h"                    // kafkaAckSend, kafkaAckSendParseFail
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
// ackOffsetAppend - append this message's {partition, offset} to the (comma-joined) offset buffer.
// Best-effort and bounded: if the fixed buffer is nearly full the entry is dropped (the offsets are
// a reference, not the durability record).
//
static void ackOffsetAppend(char* buf, int bufSize, int* posP, rd_kafka_message_t* msg)
{
  int pos = *posP;

  if (pos >= bufSize - 64)   // leave room; best-effort
    return;

  int remaining = bufSize - pos;
  int n = snprintf(&buf[pos], remaining, "%s{\"partition\":%d,\"offset\":%lld}",
                   (pos > 0) ? "," : "", (int) msg->partition, (long long) msg->offset);

  if ((n > 0) && (n < remaining))
    *posP = pos + n;
}



// -----------------------------------------------------------------------------
//
// ackBatchIdAppend - collect the producer-supplied 'x-batch-id' header (deduplicated), if present.
//
static void ackBatchIdAppend(char* buf, int bufSize, int* posP, rd_kafka_message_t* msg)
{
  rd_kafka_headers_t* hdrs = NULL;

  if (rd_kafka_message_headers(msg, &hdrs) != RD_KAFKA_RESP_ERR_NO_ERROR)
    return;

  const void* val  = NULL;
  size_t      size = 0;

  if (rd_kafka_header_get_last(hdrs, "x-batch-id", &val, &size) != RD_KAFKA_RESP_ERR_NO_ERROR)
    return;
  if ((val == NULL) || (size == 0))
    return;

  char token[256];
  int  tlen = snprintf(token, sizeof(token), "\"%.*s\"", (int) size, (const char*) val);
  if ((tlen <= 0) || (tlen >= (int) sizeof(token)))
    return;

  if (strstr(buf, token) != NULL)   // dedup - one id repeats across a batch's messages
    return;

  int pos       = *posP;
  int remaining = bufSize - pos;
  int n = snprintf(&buf[pos], remaining, "%s%s", (pos > 0) ? "," : "", token);

  if ((n > 0) && (n < remaining))
    *posP = pos + n;
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

    // Kafka ACK/NACK feedback (only when -kafkaAckTopic is set). Fixed C buffers (comma-joined inner
    // content, wrapped in [] at send time). Best-effort/bounded: a huge batch may truncate the offset
    // list - fine, it is a reference, not the durability record.
    const bool  ackEnabled     = (kafkaAckTopic[0] != 0);
    char        ackOffsets[2048];   int ackOffsetsPos  = 0;   ackOffsets[0]  = 0;
    char        ackBatchIds[1024];  int ackBatchIdsPos = 0;   ackBatchIds[0] = 0;

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

            if (ackEnabled)
            {
              ackOffsetAppend(ackOffsets, sizeof(ackOffsets), &ackOffsetsPos, msg);
              ackBatchIdAppend(ackBatchIds, sizeof(ackBatchIds), &ackBatchIdsPos, msg);
            }
          }
          else
          {
            KT_W("Kafka message discarded (parse failed), %d bytes from partition %d, offset %ld",
                 (int) msg->len, msg->partition, (long) msg->offset);

            // A message that cannot even be parsed is the clearest poison - capture the raw bytes so
            // it is not silently lost (its offset gets committed by a later successful batch).
            if (ackEnabled)
            {
              char off[80];
              snprintf(off, sizeof(off), "[{\"partition\":%d,\"offset\":%lld}]", (int) msg->partition, (long long) msg->offset);
              kafkaAckSendParseFail(off, "[]", (const char*) msg->payload, (int) msg->len);
            }
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
    // Render the batch entities to a HEAP buffer NOW - requestCompleted() below frees the kalloc
    // arena that entityArray lives in, so a NACK afterwards could no longer serialize them. Only when
    // feedback is enabled (opt-in) and there is something to render.
    char* ackEntities = NULL;
    if (ackEnabled && (entityCount > 0))
    {
      int renderSize = kjFastRenderSize(entityArray);
      ackEntities = (char*) malloc(renderSize);
      if (ackEntities != NULL)
        kjFastRender(entityArray, ackEntities);
    }

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

      //
      // Kafka feedback (Kafka path only): ACK a fully-durable batch, NACK otherwise - carrying the
      // failed entities so a poison batch is not lost even when a later batch's commit skips its offset.
      //
      if (ackEnabled)
      {
        char offsetsJson[2100];
        char batchIdsJson[1100];
        snprintf(offsetsJson,  sizeof(offsetsJson),  "[%s]", ackOffsets);
        snprintf(batchIdsJson, sizeof(batchIdsJson), "[%s]", ackBatchIds);

        if (mongoOk && (orionldState.troeError == false))
          kafkaAckSend(true, entityCount, offsetsJson, batchIdsJson, NULL, NULL);
        else
        {
          // Carry the underlying broker error (the Postgres text captured by pgCommands, or the Mongo
          // ProblemDetails) so the NACK is actionable instead of a bare category.
          char nackError[600];

          if (mongoOk)  // MongoDB ok, TRoE (Postgres) write failed
          {
            if (orionldState.troeErrorString[0] != 0)
              snprintf(nackError, sizeof(nackError), "TRoE (Postgres) write failed: %s", orionldState.troeErrorString);
            else
              snprintf(nackError, sizeof(nackError), "TRoE (Postgres) write failed");
          }
          else  // MongoDB upsert failed
          {
            if ((orionldState.pd.detail != NULL) && (orionldState.pd.detail[0] != 0))
              snprintf(nackError, sizeof(nackError), "MongoDB upsert failed: %s", orionldState.pd.detail);
            else
              snprintf(nackError, sizeof(nackError), "MongoDB upsert failed");
          }

          kafkaAckSend(false, entityCount, offsetsJson, batchIdsJson, nackError, ackEntities);
        }
      }
    }

    if (ackEntities != NULL)
      free(ackEntities);
  }

  KT_I("Kafka consumer loop exiting");
  return NULL;
}
