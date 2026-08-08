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
#include <stdlib.h>                                              // calloc, atoi
#include <string.h>                                              // strdup

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
}

#include "orionld/types/MqttInfo.h"                              // MqttInfo
#include "orionld/types/OrionldMimeType.h"                       // MimeType, mimeTypeFromString
#include "orionld/types/Protocol.h"                              // Protocol, protocolFromString
#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/urlParse.h"                             // urlParse
#include "orionld/mqtt/mqttParse.h"                              // mqttParse
#include "orionld/subCache/subCacheItemStatusSet.h"              // subCacheItemStatusSet
#include "orionld/subCache/subCacheItemEndpointCompile.h"        // Own interface



// -----------------------------------------------------------------------------
//
// mqttCompile - an MQTT endpoint URI, split the way the MQTT client needs it
//
// The URI carries user, password, host, port and topic; the QoS and the protocol
// version are not part of it - they come from "notifierInfo".
//
static void mqttCompile(SubCacheItem* sciP, const char* uri, KjNode* notifierInfoP)
{
  char   url[512];
  char*  user     = NULL;
  char*  password = NULL;
  char*  host     = NULL;
  char*  topic    = NULL;
  char*  detail   = NULL;
  bool   mqtts    = false;
  uint16_t port   = 0;

  strncpy(url, uri, sizeof(url) - 1);
  url[sizeof(url) - 1] = 0;

  if (mqttParse(url, &mqtts, &user, &password, &host, &port, &topic, &detail) == false)
  {
    subCacheItemStatusSet(sciP, "paused");
    KT_RVE("Sub '%s': invalid MQTT endpoint URI ('%s'): %s", sciP->subId, uri, (detail != NULL)? detail : "no detail");
  }

  sciP->mqttP = (MqttInfo*) calloc(1, sizeof(MqttInfo));

  if (sciP->mqttP == NULL)
    KT_X(1, "Out of memory attempting to allocate the MQTT info of a Subscription (%d bytes)", sizeof(MqttInfo));

  sciP->mqttP->mqtts = mqtts;
  sciP->mqttP->port  = port;

  if (user     != NULL) strncpy(sciP->mqttP->username, user,     sizeof(sciP->mqttP->username) - 1);
  if (password != NULL) strncpy(sciP->mqttP->password, password, sizeof(sciP->mqttP->password) - 1);
  if (host     != NULL) strncpy(sciP->mqttP->host,     host,     sizeof(sciP->mqttP->host)     - 1);
  if (topic    != NULL) strncpy(sciP->mqttP->topic,    topic,    sizeof(sciP->mqttP->topic)    - 1);

  if (notifierInfoP != NULL)
  {
    for (KjNode* niP = notifierInfoP->value.firstChildP; niP != NULL; niP = niP->next)
    {
      KjNode* keyP   = kjLookup(niP, "key");
      KjNode* valueP = kjLookup(niP, "value");

      if ((keyP == NULL) || (valueP == NULL))
        continue;

      if      (strcmp(keyP->value.s, "MQTT-QoS")     == 0) sciP->mqttP->qos = atoi(valueP->value.s);
      else if (strcmp(keyP->value.s, "MQTT-Version") == 0) strncpy(sciP->mqttP->version, valueP->value.s, sizeof(sciP->mqttP->version) - 1);
    }
  }

  KT_T(KtSubCache, "Sub '%s': MQTT endpoint (host: '%s', port: %d, topic: '%s', qos: %d)",
       sciP->subId, sciP->mqttP->host, sciP->mqttP->port, sciP->mqttP->topic, sciP->mqttP->qos);
}



// -----------------------------------------------------------------------------
//
// subCacheItemEndpointCompile -
//
void subCacheItemEndpointCompile(SubCacheItem* sciP, KjNode* endpointP)
{
  KjNode* uriP    = kjLookup(endpointP, "uri");
  KjNode* acceptP = kjLookup(endpointP, "accept");

  //
  // "accept" is optional, "uri" is not - pCheckSubscription makes sure of that,
  // but a subscription straight from the database has seen no pCheckSubscription.
  //
  // mimeTypeFromString cuts its input at a ';' (charset) and needs the accept-mask
  // output parameter for wildcards - so it gets a copy of the string, not the one
  // inside the cached subTree.
  //
  if (acceptP != NULL)
  {
    char      accept[64];
    uint32_t  acceptMask = 0;

    strncpy(accept, acceptP->value.s, sizeof(accept) - 1);
    accept[sizeof(accept) - 1] = 0;

    sciP->mimeType = mimeTypeFromString(accept, NULL, true, false, &acceptMask);
  }
  else
    sciP->mimeType = MT_JSON;

  if (uriP == NULL)
    KT_RVE("Sub '%s': no 'notification::endpoint::uri' - the subscription can never notify", sciP->subId);

  //
  // urlParse destroys its input and hands back pointers into it - so it gets a
  // copy of the URI that the cache item owns.
  //
  sciP->url = strdup(uriP->value.s);

  if (urlParse(sciP->url, &sciP->protocolString, &sciP->ip, &sciP->port, &sciP->rest) == false)
    KT_RVE("Sub '%s': invalid 'notification::endpoint::uri' ('%s')", sciP->subId, uriP->value.s);

  sciP->protocol = protocolFromString(sciP->protocolString);

  KT_T(KtSubCache, "Sub '%s': endpoint protocol: '%s', IP: '%s', port: %d, rest: '%s'",
       sciP->subId, sciP->protocolString, sciP->ip, sciP->port, sciP->rest);

  if ((sciP->protocol == MQTT) || (sciP->protocol == MQTTS))
    mqttCompile(sciP, uriP->value.s, kjLookup(endpointP, "notifierInfo"));
}
