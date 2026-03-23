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
#include <stdio.h>                                             // snprintf
#include <string.h>                                            // strlen, strcmp, strncmp, strdup
#include <stdlib.h>                                            // malloc, free
#include <pthread.h>                                           // pthread_mutex_*
#include <MQTTClient.h>                                        // Paho MQTT C client

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, kjArray, kjChildAdd, kjInteger
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjClone.h"                                     // kjClone
#include "kjson/kjRender.h"                                    // kjRender
#include "kjson/kjRenderSize.h"                                // kjRenderSize
}

#include "common/orionldState.h"                               // orionldState
#include "common/traceLevels.h"                                // Trace levels
#include "ftClient/mqttClient.h"                               // Own interface



// -----------------------------------------------------------------------------
//
// MqttSubscription - tracks an MQTT subscription in the test client
//
typedef struct MqttSubscription
{
  char*                       topic;
  char*                       host;
  unsigned short              port;
  bool                        mqtts;
  MQTTClient                  client;
  KjNode*                     notifications;   // Array of accumulated notifications
  pthread_mutex_t             mutex;
  volatile bool               active;
  pthread_t                   recvThread;
  struct MqttSubscription*    next;
} MqttSubscription;

static MqttSubscription*  mqttSubList      = NULL;
static pthread_mutex_t    mqttSubListMutex = PTHREAD_MUTEX_INITIALIZER;



// -----------------------------------------------------------------------------
//
// mqttSubLookup - find a subscription by topic
//
static MqttSubscription* mqttSubLookup(const char* topic)
{
  pthread_mutex_lock(&mqttSubListMutex);

  MqttSubscription* p = mqttSubList;
  while (p != NULL)
  {
    if ((p->topic != NULL) && (strcmp(p->topic, topic) == 0))
    {
      pthread_mutex_unlock(&mqttSubListMutex);
      return p;
    }
    p = p->next;
  }

  pthread_mutex_unlock(&mqttSubListMutex);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// mqttSubAdd - add to the global list
//
static void mqttSubAdd(MqttSubscription* msP)
{
  pthread_mutex_lock(&mqttSubListMutex);
  msP->next   = mqttSubList;
  mqttSubList = msP;
  pthread_mutex_unlock(&mqttSubListMutex);
}



// -----------------------------------------------------------------------------
//
// mqttMessageArrived - Paho MQTT callback for incoming messages
//
static int mqttMessageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message)
{
  MqttSubscription* msP = (MqttSubscription*) context;

  char* payload = (char*) malloc(message->payloadlen + 1);
  memcpy(payload, message->payload, message->payloadlen);
  payload[message->payloadlen] = 0;

  KT_V("MQTT message on topic '%s': %s", topicName, payload);

  // Parse JSON and accumulate
  orionldStateInit(NULL);
  KjNode* notif = kjParse(orionldState.kjsonP, payload);

  if (notif != NULL)
  {
    notif = kjClone(NULL, notif);

    pthread_mutex_lock(&msP->mutex);
    if (msP->notifications == NULL)
      msP->notifications = kjArray(NULL, NULL);
    kjChildAdd(msP->notifications, notif);
    pthread_mutex_unlock(&msP->mutex);
  }

  free(payload);
  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);
  return 1;
}



// -----------------------------------------------------------------------------
//
// mqttConnectionLost - Paho MQTT callback for lost connections
//
static void mqttConnectionLost(void* context, char* cause)
{
  MqttSubscription* msP = (MqttSubscription*) context;
  KT_W("MQTT connection lost for topic '%s': %s", msP->topic, cause ? cause : "unknown");
  msP->active = false;
}



// -----------------------------------------------------------------------------
//
// postMqttSub - POST /mqtt/sub
//
// Body: { "host": "localhost", "port": 1883, "topic": "entities", "mqtts": false, "caFile": "/path/to/ca.crt" }
//
extern __thread KjNode* uriParams;

KjNode* postMqttSub(int* statusCodeP)
{
  if (orionldState.requestTree == NULL)
  {
    *statusCodeP = 400;
    return kjString(orionldState.kjsonP, "error", "No payload body");
  }

  KjNode* hostP   = kjLookup(orionldState.requestTree, "host");
  KjNode* portP   = kjLookup(orionldState.requestTree, "port");
  KjNode* topicP  = kjLookup(orionldState.requestTree, "topic");
  KjNode* mqttsP  = kjLookup(orionldState.requestTree, "mqtts");
  KjNode* caFileP = kjLookup(orionldState.requestTree, "caFile");

  const char*    host   = (hostP   != NULL && hostP->type   == KjString)  ? hostP->value.s      : "localhost";
  int            port   = (portP   != NULL && portP->type   == KjInt)     ? portP->value.i      : 1883;
  const char*    topic  = (topicP  != NULL && topicP->type  == KjString)  ? topicP->value.s     : "notification";
  bool           mqtts  = (mqttsP  != NULL && mqttsP->type  == KjBoolean) ? mqttsP->value.b     : false;
  const char*    caFile = (caFileP != NULL && caFileP->type == KjString)  ? caFileP->value.s    : NULL;

  // Check if already subscribed
  if (mqttSubLookup(topic) != NULL)
  {
    *statusCodeP = 409;
    return kjString(orionldState.kjsonP, "error", "Already subscribed to this topic");
  }

  // Build address string
  char address[128];
  if (mqtts)
    snprintf(address, sizeof(address), "ssl://%s:%d", host, port);
  else
    snprintf(address, sizeof(address), "tcp://%s:%d", host, port);

  // Create client
  MqttSubscription* msP = (MqttSubscription*) malloc(sizeof(MqttSubscription));
  memset(msP, 0, sizeof(MqttSubscription));
  msP->topic = strdup(topic);
  msP->host  = strdup(host);
  msP->port  = (unsigned short) port;
  msP->mqtts = mqtts;
  pthread_mutex_init(&msP->mutex, NULL);

  char clientId[64];
  snprintf(clientId, sizeof(clientId), "ftClient-%s-%d", topic, getpid());

  int rc = MQTTClient_create(&msP->client, address, clientId, MQTTCLIENT_PERSISTENCE_NONE, NULL);
  if (rc != MQTTCLIENT_SUCCESS)
  {
    free(msP->topic);
    free(msP->host);
    free(msP);
    *statusCodeP = 500;
    return kjString(orionldState.kjsonP, "error", "MQTTClient_create failed");
  }

  // Set callbacks
  MQTTClient_setCallbacks(msP->client, msP, mqttConnectionLost, mqttMessageArrived, NULL);

  // Connect
  MQTTClient_connectOptions connOpts = MQTTClient_connectOptions_initializer;
  connOpts.keepAliveInterval = 20;
  connOpts.cleansession      = 1;

  MQTTClient_SSLOptions sslOpts = MQTTClient_SSLOptions_initializer;
  if (mqtts)
  {
    if (caFile != NULL)
    {
      sslOpts.trustStore           = caFile;
      sslOpts.enableServerCertAuth = 1;
    }
    else
    {
      sslOpts.enableServerCertAuth = 0;
    }
    connOpts.ssl = &sslOpts;
  }

  rc = MQTTClient_connect(msP->client, &connOpts);
  if (rc != MQTTCLIENT_SUCCESS)
  {
    KT_E("MQTT connect to %s failed: %d", address, rc);
    MQTTClient_destroy(&msP->client);
    free(msP->topic);
    free(msP->host);
    free(msP);
    *statusCodeP = 502;

    char detail[256];
    snprintf(detail, sizeof(detail), "Cannot connect to MQTT broker at %s (error %d)", address, rc);
    return kjString(orionldState.kjsonP, "error", detail);
  }

  // Subscribe to topic
  rc = MQTTClient_subscribe(msP->client, topic, 0);
  if (rc != MQTTCLIENT_SUCCESS)
  {
    MQTTClient_disconnect(msP->client, 1000);
    MQTTClient_destroy(&msP->client);
    free(msP->topic);
    free(msP->host);
    free(msP);
    *statusCodeP = 500;
    return kjString(orionldState.kjsonP, "error", "MQTTClient_subscribe failed");
  }

  msP->active = true;
  mqttSubAdd(msP);

  KT_V("MQTT subscribed to '%s' on %s", topic, address);

  *statusCodeP = 201;
  KjNode* result = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(result, kjString(orionldState.kjsonP, "topic", topic));
  kjChildAdd(result, kjString(orionldState.kjsonP, "broker", address));
  kjChildAdd(result, kjBoolean(orionldState.kjsonP, "mqtts", mqtts));

  return result;
}



// -----------------------------------------------------------------------------
//
// mqttRouteDispatch - dispatch MQTT routes with dynamic topic
//
// URL pattern: /mqtt/{topic}/{action}
// Actions: dump (GET/DELETE), unsub (POST)
//
KjNode* mqttRouteDispatch(int* statusCodeP)
{
  char* path = orionldState.urlPath;

  if (strncmp(path, "/mqtt/", 6) != 0)
    return NULL;

  // Extract topic and action
  char* topicStart = &path[6];
  char* slash      = strchr(topicStart, '/');
  if (slash == NULL)
    return NULL;

  *slash = 0;
  char* topic  = topicStart;
  char* action = &slash[1];

  MqttSubscription* msP = mqttSubLookup(topic);

  if (strcmp(action, "dump") == 0 && orionldState.verb == HTTP_GET)
  {
    // GET /mqtt/{topic}/dump - return accumulated notifications
    *statusCodeP = 200;

    if (msP == NULL)
      return kjArray(orionldState.kjsonP, NULL);

    pthread_mutex_lock(&msP->mutex);
    KjNode* result = (msP->notifications != NULL) ?
                     kjClone(orionldState.kjsonP, msP->notifications) :
                     kjArray(orionldState.kjsonP, NULL);
    pthread_mutex_unlock(&msP->mutex);

    return result;
  }
  else if (strcmp(action, "dump") == 0 && orionldState.verb == HTTP_DELETE)
  {
    // DELETE /mqtt/{topic}/dump - reset accumulated notifications
    if (msP == NULL)
    {
      *statusCodeP = 404;
      return kjString(orionldState.kjsonP, "error", "MQTT subscription not found");
    }

    pthread_mutex_lock(&msP->mutex);
    msP->notifications = NULL;
    pthread_mutex_unlock(&msP->mutex);

    *statusCodeP = 204;
    return NULL;
  }
  else if (strcmp(action, "unsub") == 0 && orionldState.verb == HTTP_POST)
  {
    // POST /mqtt/{topic}/unsub - unsubscribe and disconnect
    if (msP == NULL)
    {
      *statusCodeP = 404;
      return kjString(orionldState.kjsonP, "error", "MQTT subscription not found");
    }

    msP->active = false;
    MQTTClient_unsubscribe(msP->client, msP->topic);
    MQTTClient_disconnect(msP->client, 1000);
    MQTTClient_destroy(&msP->client);

    *statusCodeP = 204;
    return NULL;
  }

  *statusCodeP = 400;
  return kjString(orionldState.kjsonP, "error", "Unknown MQTT action");
}
