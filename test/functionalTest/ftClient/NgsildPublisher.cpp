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
*/

//
// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                 // kaAlloc
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjRender.h"                                 // kjFastRender
#include "kjson/kjRenderSize.h"                             // kjFastRenderSize
#include "kjson/kjBuilder.h"                                // kjChildRemove
}

#include "orionld/common/traceLevels.h"                     // Trace Levels
#include "orionld/common/orionldState.h"                    // orionldState
#include "ftClient/NgsildPublisher.h"                    // NgsildPublisher
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2



// -----------------------------------------------------------------------------
//
// NgsildPublisher::~NgsildPublisher
//
NgsildPublisher::~NgsildPublisher()
{
  if (writer_ != nullptr)
    publisher_->delete_datawriter(writer_);

  if (publisher_ != nullptr)
    participant_->delete_publisher(publisher_);

  if (topic_ != nullptr)
    participant_->delete_topic(topic_);

  DomainParticipantFactory::get_instance()->delete_participant(participant_);
}



// -----------------------------------------------------------------------------
//
// NgsildPublisher::init -
//
// FIXME: need to move the params to the constructor, as the constructor creates the
//        NgsildEntityPubSubType where currently the topic name is hardcoded to "NgsildEntity".
//
bool NgsildPublisher::init(const char* topicName)
{
  DomainParticipantQos  participantQos;

  participantQos.name("Participant_publisher");
  participant_ = DomainParticipantFactory::get_instance()->create_participant(0, participantQos);

  if (participant_ == nullptr)
  {
    KT_E("Create participant failed");
    return false;
  }

  // Register the Type
  type_.register_type(participant_);

  // Create the publications Topic
  const char* topicType = type_->get_name().c_str();
  KT_V("creating topic (type: '%s') '%s'", topicType, topicName);
  topic_ = participant_->create_topic(topicName, topicType, TOPIC_QOS_DEFAULT);

  if (topic_ == nullptr)
  {
    KT_E("Create topic %s/%s failed", topicType, topicName);
    return false;
  }
  KT_V("created topic (type: '%s') '%s'", topicType, topicName);

  // Create the Publisher
  publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT, nullptr);
  KT_V("created publisher");

  if (publisher_ == nullptr)
  {
    KT_E("Create publisher failed");
    return false;
  }

  // Create the DataWriter
  KT_V("Creating writer");

  DataWriterQos  wqos = DATAWRITER_QOS_DEFAULT;

  wqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
  wqos.durability().kind  = eprosima::fastdds::dds::TRANSIENT_LOCAL_DURABILITY_QOS;
  wqos.history().kind     = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
  wqos.history().depth    = 5;
  writer_                 = publisher_->create_datawriter(topic_, wqos, &listener_);

  if (writer_ == nullptr)
  {
    KT_E("Error creating DataWriter");
    return false;
  }

  KT_V("Created writer");
  KT_V("Init done");

  return true;
}



// -----------------------------------------------------------------------------
//
// NgsildPublisher::publish -
//
// IMPORTANT:
//   The input "entityP" is a 100% NGSI-LD Entity.
//   This function takes care of moving all attributes into the field "attributes", the way we need it for DDS
//
bool NgsildPublisher::publish(const char* entityType, const char* entityId, const char* topicName, const char* s, int i, double f, bool b)
{
  KT_V("Publishing an attribute (%s)", topicName);

  int attempts    = 1;
  int maxAttempts = 1000;  // FIXME: sleeping here for one whole second it just not good enough!!!

  while ((listener_.ready_ == false) && (attempts < maxAttempts))
  {
    usleep(1000);  // One millisecond
    ++attempts;
  }

  if (listener_.ready_ == false)
  {
    KT_W("listener still not ready after waiting for %d milliseconds", attempts);
    return false;
  }

  if (listener_.matched_ <= 0)
  {
    KT_W("listener not matched");
    return false;
  }

  KT_V("s:     %s", s);
  KT_V("i:     %d", i);
  KT_V("f:     %f", f);
  KT_V("b:     %d", b);

  if (s != NULL) entity_.s(s);
  entity_.i(i);
  entity_.f(f);
  entity_.b(b);

  eprosima::fastdds::dds::ReturnCode_t rc = writer_->write(&entity_);

  if (rc != eprosima::fastdds::dds::RETCODE_OK)
  {
    KT_E("Not able to publish");
    return false;
  }

  eprosima::fastdds::dds::Duration_t    duration(0, 10000000);  // 0.01 seconds
  eprosima::fastdds::dds::ReturnCode_t  r = writer_->wait_for_acknowledgments(duration);

  if (r == eprosima::fastdds::dds::RETCODE_OK)
    KT_V("writer has successfully published an entity");
  else if  (r == eprosima::fastdds::dds::RETCODE_TIMEOUT)
    KT_W("wait_for_acknowledgments timed out (10 milliseconds)");
  else
    KT_E("wait_for_acknowledgments failed with error %d", r);

  return true;
}
