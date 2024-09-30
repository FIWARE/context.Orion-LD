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
#include <unistd.h>                                         // usleep

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
}

#include "orionld/common/traceLevels.h"                     // KT_T trace levels
#include "ftClient/NgsildSample.hpp"                        // NgsildSample
#include "ftClient/NgsildPublisher.h"                       // NgsildPublisher

#include "ftClient/NgsildSamplePubSubTypes.hpp"             // NgsildSamplePubSubTypes



#if 0
// -----------------------------------------------------------------------------
//
// ddsPublishAttribute -
//
// entityType is optional (NULL)
// entityId   is mandatory
//
void ddsPublishAttribute
(
  const char*  topicType,
  const char*  topicName,
  const char*  entityType,
  const char*  entityId,
  const char*  s,
  int          i,
  double       f,
  bool         b
)
{
  NgsildPublisher* publisherP = new NgsildPublisher(topicType);

  KT_V("Initializing publisher for topicType '%s', topicName '%s'", topicType, topicName);
  if (publisherP->init(topicName))
  {
    //
    // FIXME: we can't do new+publish+delete for each and every publication!
    // There might easily be 10,000 publications per second.
    //


    KT_V("Publishing on topicType '%s', topicName '%s'", topicType, topicName);
    if (publisherP->publish(entityType, entityId, topicName, s, i, f, b))
      KT_V("Published on topicType '%s', topicName '%s'", topicType, topicName);
    else
      KT_V("Error publishing on topicType '%s', topicName '%s'", topicType, topicName);
  }
  else
    KT_E("NgsildPublisher::init failed (get error string from DDS)");

  KT_V("Deleting publisher");
  delete publisherP;
}



// -----------------------------------------------------------------------------
//
// ddsPublishEntity -
//
void ddsPublishEntity
(
  const char* topicType,
  const char* entityType,
  const char* entityId,
  KjNode*     entityP
)
{
  const char* topicName = entityP->name;

  KT_V("Publishing the attributes of the entity '%s' in DDS", entityId);
  for (KjNode* attributeP = entityP->value.firstChildP; attributeP != NULL; attributeP = attributeP->next)
  {
    if (strcmp(attributeP->name, "s") == 0)
      ddsPublishAttribute(topicType, topicName, entityType, entityId, attributeP->value.s, 0, 0, false);
    else if (strcmp(attributeP->name, "i") == 0)
      ddsPublishAttribute(topicType, topicName, entityType, entityId, NULL, attributeP->value.i, 0, false);
    else if (strcmp(attributeP->name, "i") == 0)
      ddsPublishAttribute(topicType, topicName, entityType, entityId, NULL, 0, attributeP->value.f, false);
    else if (strcmp(attributeP->name, "b") == 0)
      ddsPublishAttribute(topicType, topicName, entityType, entityId, NULL, 0, 0, true);
  }
}
#endif

// -----------------------------------------------------------------------------
//
// ddsPublishEntity -
//
void ddsPublishEntity
(
  const char* topicType,
  const char* topicName,
  const char* entityType,
  const char* entityId,
  KjNode*     entityP
)
{
  KT_V("Publishing the entity '%s' in DDS", entityId);

  NgsildPublisher* publisherP = new NgsildPublisher(topicType);
  usleep(100000);

  KT_V("Initializing publisher for topicType '%s', topicName '%s'", topicType, topicName);
  if (publisherP->init(topicName))
  {
    char*  s = NULL;
    int    i = 0;
    double f = 0;
    bool   b  = false;

    for (KjNode* attributeP = entityP->value.firstChildP; attributeP != NULL; attributeP = attributeP->next)
    {
      if      (strcmp(attributeP->name, "s") == 0)      s = attributeP->value.s;
      else if (strcmp(attributeP->name, "i") == 0)      i = attributeP->value.i;
      else if (strcmp(attributeP->name, "f") == 0)      i = attributeP->value.f;
      else if (strcmp(attributeP->name, "b") == 0)      i = attributeP->value.b;
    }

    publisherP->publish(entityType, entityId, topicName, s, i, f, b);
  }
  else
    KT_E("NgsildPublisher::init failed (get error string from DDS)");

  KT_V("Deleting publisher");
  delete publisherP;
}
