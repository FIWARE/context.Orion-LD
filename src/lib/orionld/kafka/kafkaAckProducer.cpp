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
#include <librdkafka/rdkafka.h>                                // rd_kafka_*
#include <stdlib.h>                                            // malloc, free
#include <string.h>                                            // strlen
#include <stdio.h>                                             // snprintf

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/common/traceLevels.h"                        // KtKafka
#include "orionld/common/orionldState.h"                       // kafkaAckTopic (declared here, defined in orionld.cpp)
#include "orionld/kafka/kafkaAckProducer.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// External references - kafkaBrokerList lives in orionld.cpp (not in orionldState.h)
//
extern char kafkaBrokerList[];



// -----------------------------------------------------------------------------
//
// kafkaAckProducerHandle - the ACK/NACK producer (separate from the consumer handle)
//
static rd_kafka_t* kafkaAckProducerHandle = NULL;



// -----------------------------------------------------------------------------
//
// kafkaAckProducerInit -
//
bool kafkaAckProducerInit(void)
{
  if (kafkaAckTopic[0] == 0)
  {
    KT_I("Kafka ACK/NACK feedback disabled (no -kafkaAckTopic)");
    return true;   // feature off - not an error
  }

  char              errstr[512];
  rd_kafka_conf_t*  conf = rd_kafka_conf_new();

  if (conf == NULL)
  {
    KT_E("kafkaAckProducerInit: failed to create producer configuration");
    return false;
  }

  if (rd_kafka_conf_set(conf, "bootstrap.servers", kafkaBrokerList, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
  {
    KT_E("kafkaAckProducerInit: failed to set bootstrap.servers: %s", errstr);
    rd_kafka_conf_destroy(conf);
    return false;
  }

  // The feedback record must itself be durable - idempotent producer (forces acks=all).
  rd_kafka_conf_set(conf, "enable.idempotence", "true", errstr, sizeof(errstr));

  kafkaAckProducerHandle = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
  if (kafkaAckProducerHandle == NULL)
  {
    KT_E("kafkaAckProducerInit: failed to create producer: %s", errstr);  // conf freed by rd_kafka_new on failure
    return false;
  }

  KT_I("Kafka ACK/NACK producer initialized (topic '%s')", kafkaAckTopic);
  return true;
}



// -----------------------------------------------------------------------------
//
// kafkaAckProducerRelease -
//
void kafkaAckProducerRelease(void)
{
  if (kafkaAckProducerHandle == NULL)
    return;

  rd_kafka_flush(kafkaAckProducerHandle, 5000);
  rd_kafka_destroy(kafkaAckProducerHandle);
  kafkaAckProducerHandle = NULL;
}



// -----------------------------------------------------------------------------
//
// jsonEscapeAppend - minimal JSON string escaper for the short, broker-controlled error text
//
static void jsonEscapeAppend(char* out, int outSize, int* posP, const char* in)
{
  int pos = *posP;

  for (const char* p = in; (*p != 0) && (pos < outSize - 2); ++p)
  {
    char c = *p;

    if      ((c == '"') || (c == '\\')) { out[pos++] = '\\'; out[pos++] = c;   }
    else if (c == '\n')                 { out[pos++] = '\\'; out[pos++] = 'n'; }
    else if (c == '\r')                 { out[pos++] = '\\'; out[pos++] = 'r'; }
    else if (c == '\t')                 { out[pos++] = '\\'; out[pos++] = 't'; }
    else if ((unsigned char) c >= 0x20) { out[pos++] = c;                      }
    // other control chars are dropped
  }

  *posP = pos;
}



// -----------------------------------------------------------------------------
//
// kafkaAckSend -
//
void kafkaAckSend(bool ok, int count, const char* offsetsJson, const char* batchIds, const char* error, const char* entitiesJson)
{
  if (kafkaAckProducerHandle == NULL)   // feature disabled
    return;

  if ((offsetsJson == NULL) || (offsetsJson[0] == 0)) offsetsJson = "[]";
  if ((batchIds    == NULL) || (batchIds[0]    == 0)) batchIds    = "[]";

  //
  // Assemble the feedback JSON. entitiesJson is already valid JSON (kjFastRender output) and is
  // spliced in verbatim; only 'error' (a short broker string) is escaped. bufSize is sized to hold
  // every part at full length plus the fixed scaffolding, so the bounded snprintf/escape never
  // overrun and 'pos' stays < bufSize.
  //
  size_t  entitiesLen = (entitiesJson != NULL) ? strlen(entitiesJson) : 0;
  size_t  errorLen    = (error != NULL) ? strlen(error) : 0;
  int     bufSize     = (int) (256 + strlen(offsetsJson) + strlen(batchIds) + entitiesLen + errorLen * 2);
  char*   buf         = (char*) malloc(bufSize);

  if (buf == NULL)
  {
    KT_E("kafkaAckSend: out of memory (%d bytes)", bufSize);
    return;
  }

  int pos = snprintf(buf, bufSize, "{\"status\":\"%s\",\"count\":%d,\"offsets\":%s,\"batchIds\":%s",
                     ok ? "ack" : "nack", count, offsetsJson, batchIds);

  if ((error != NULL) && (error[0] != 0))
  {
    pos += snprintf(&buf[pos], bufSize - pos, ",\"error\":\"");
    jsonEscapeAppend(buf, bufSize, &pos, error);
    pos += snprintf(&buf[pos], bufSize - pos, "\"");
  }

  if ((entitiesJson != NULL) && (entitiesJson[0] != 0))
    pos += snprintf(&buf[pos], bufSize - pos, ",\"entities\":%s", entitiesJson);

  pos += snprintf(&buf[pos], bufSize - pos, "}");

  //
  // RD_KAFKA_MSG_F_COPY: librdkafka copies the payload, so we can free 'buf' right after the call
  // (without the flag, producev references the buffer until the delivery report).
  //
  rd_kafka_resp_err_t err = rd_kafka_producev(kafkaAckProducerHandle,
                                              RD_KAFKA_V_TOPIC(kafkaAckTopic),
                                              RD_KAFKA_V_VALUE(buf, strlen(buf)),
                                              RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                                              RD_KAFKA_V_END);
  if (err != RD_KAFKA_RESP_ERR_NO_ERROR)
    KT_W("kafkaAckSend: produce to '%s' failed: %s", kafkaAckTopic, rd_kafka_err2str(err));
  else
    KT_T(KtKafka, "Kafka %s sent (%d entities) to '%s'", ok ? "ACK" : "NACK", count, kafkaAckTopic);

  rd_kafka_poll(kafkaAckProducerHandle, 0);   // serve delivery callbacks
  free(buf);
}



// -----------------------------------------------------------------------------
//
// kafkaAckSendParseFail -
//
void kafkaAckSendParseFail(const char* offsetsJson, const char* batchIds, const char* rawPayload, int rawLen)
{
  if (kafkaAckProducerHandle == NULL)   // feature disabled
    return;
  if ((rawPayload == NULL) || (rawLen <= 0))
    return;

  // Build ["<escaped raw>"] as a valid JSON array so it can ride in the 'entities' field verbatim.
  int   cap = rawLen * 2 + 8;
  char* raw = (char*) malloc(rawLen + 1);
  char* arr = (char*) malloc(cap);

  if ((raw == NULL) || (arr == NULL))
  {
    free(raw);
    free(arr);
    return;
  }

  memcpy(raw, rawPayload, rawLen);
  raw[rawLen] = 0;

  int pos = 0;
  arr[pos++] = '[';
  arr[pos++] = '"';
  jsonEscapeAppend(arr, cap, &pos, raw);
  arr[pos++] = '"';
  arr[pos++] = ']';
  arr[pos]   = 0;

  kafkaAckSend(false, 1, offsetsJson, batchIds, "Kafka message parse failed", arr);

  free(raw);
  free(arr);
}
