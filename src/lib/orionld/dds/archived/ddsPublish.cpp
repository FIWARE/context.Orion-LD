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
extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
}

#include "orionld/common/traceLevels.h"                     // KT_T trace levels
#include "orionld/dds/NgsildEntityPubSubTypes.h"            // NgsildEntityPubSubTypes
#include "orionld/dds/NgsildEntity.h"                       // NgsildEntity
#include "orionld/dds/NgsildPublisher.h"                    // NgsildPublisher
#include "orionld/dds/config.h"                             // DDS_RELIABLE, ...



// -----------------------------------------------------------------------------
//
// namespaces ... (to be removed!)
//
using namespace eprosima::fastdds::dds;



// -----------------------------------------------------------------------------
//
// ddsPublishAttribute -
//
// entityType is optional (NULL)
// entityId   is mandatory
//
void ddsPublishAttribute
(
  const char* topicType,
  const char* entityType,
  const char* entityId,
  KjNode*     attributeP
)
{
  char*            topicName  = attributeP->name;
  NgsildPublisher* publisherP = new NgsildPublisher(topicType);

  KT_V("Initializing publisher for topicType '%s', topicName '%s'", topicType, topicName);
  if (publisherP->init(topicName))
  {
    //
    // FIXME: we can't do new+publish+delete for each and every publication!
    // There might easily be 10,000 publications per second.
    //

#ifdef DDS_SLEEP
    usleep(5000);
#endif

    KT_V("Publishing on topicType '%s', topicName '%s'", topicType, topicName);
    if (publisherP->publish(entityType, entityId, attributeP))
      KT_V("Published on topicType '%s', topicName '%s'", topicType, topicName);
    else
      KT_V("Error publishing on topicType '%s', topicName '%s'", topicType, topicName);

#ifdef DDS_SLEEP
    usleep(5000);
#endif
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
  KT_V("Publishing the attributes of the entity '%s' in DDS", entityId);
  for (KjNode* attributeP = entityP->value.firstChildP; attributeP != NULL; attributeP = attributeP->next)
  {
    if (strcmp(attributeP->name, "id") == 0)
      continue;
    if (strcmp(attributeP->name, "type") == 0)
      continue;

    ddsPublishAttribute(topicType, entityType, entityId, attributeP);
  }
}
