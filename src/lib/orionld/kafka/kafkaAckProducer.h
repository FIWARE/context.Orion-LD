#ifndef SRC_LIB_ORIONLD_KAFKA_KAFKAACKPRODUCER_H_
#define SRC_LIB_ORIONLD_KAFKA_KAFKAACKPRODUCER_H_

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



// -----------------------------------------------------------------------------
//
// kafkaAckProducerInit -
//
// Creates the rd_kafka producer used to publish ACK/NACK feedback for KAFKA-INGESTED
// batches. A no-op returning true when 'kafkaAckTopic' is empty (feature disabled).
// Only ever used from the Kafka consumer path - REST ingest has its synchronous HTTP
// response as its acknowledgement and must NOT emit to Kafka.
//
extern bool kafkaAckProducerInit(void);



// -----------------------------------------------------------------------------
//
// kafkaAckProducerRelease -
//
extern void kafkaAckProducerRelease(void);



// -----------------------------------------------------------------------------
//
// kafkaAckSend -
//
// Publishes one feedback record to 'kafkaAckTopic'. No-op if the producer is disabled.
//
//   ok            true  -> ACK  (batch durable in MongoDB AND TRoE)
//                 false -> NACK (a stage failed / poison / parse error)
//   count         number of entities the record refers to
//   offsetsJson   rendered JSON array of {"partition":P,"offset":O} (may be NULL/"" -> "[]")
//   batchIds      rendered JSON array of the x-batch-id headers seen (may be NULL/"" -> "[]")
//   error         NACK only: short reason string (NULL for an ACK)
//   entitiesJson  NACK only: the failed payload as a JSON string so it is not lost
//                 (NULL for an ACK - a success needs no payload echo, the data is durable)
//
extern void kafkaAckSend(bool         ok,
                         int          count,
                         const char*  offsetsJson,
                         const char*  batchIds,
                         const char*  error,
                         const char*  entitiesJson);



// -----------------------------------------------------------------------------
//
// kafkaAckSendParseFail -
//
// NACK for a Kafka message that could not even be parsed (so there is no entity tree). The raw
// message bytes are escaped and carried as the payload so the poison message is not lost.
//
extern void kafkaAckSendParseFail(const char* offsetsJson, const char* batchIds, const char* rawPayload, int rawLen);

#endif  // SRC_LIB_ORIONLD_KAFKA_KAFKAACKPRODUCER_H_
