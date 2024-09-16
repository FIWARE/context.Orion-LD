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
#include "fastdds/dds/domain/DomainParticipant.hpp"
#include "fastdds/dds/domain/DomainParticipantFactory.hpp"
#include "fastdds/dds/subscriber/DataReader.hpp"
#include "fastdds/dds/subscriber/DataReaderListener.hpp"
#include "fastdds/dds/subscriber/qos/DataReaderQos.hpp"
#include "fastdds/dds/subscriber/SampleInfo.hpp"
#include "fastdds/dds/subscriber/Subscriber.hpp"
#include "fastdds/dds/topic/TypeSupport.hpp"

#include "orionld/dds/NgsildSubscriber.h"                           // The class

using namespace eprosima::fastdds::dds;



// -----------------------------------------------------------------------------
//
// NgsildSubscriber::~NgsildSubscriber -
//
NgsildSubscriber::~NgsildSubscriber()
{
  if (reader_ != nullptr)
    subscriber_->delete_datareader(reader_);

  if (topic_ != nullptr)
    participant_->delete_topic(topic_);

  if (subscriber_ != nullptr)
    participant_->delete_subscriber(subscriber_);

  DomainParticipantFactory::get_instance()->delete_participant(participant_);
}



// -----------------------------------------------------------------------------
//
// NgsildSubscriber::init -
//
bool NgsildSubscriber::init(const char* topicName)
{
  DomainParticipantQos participantQos;

  participantQos.name("Participant_subscriber");
  participant_ = DomainParticipantFactory::get_instance()->create_participant(0, participantQos);

  if (participant_ == nullptr)
    return false;

  // Register the Type
  type_.register_type(participant_);

  // Create the subscriptions Topic
  const char* topicType = type_->getName();
  topic_ = participant_->create_topic(topicName, topicType, TOPIC_QOS_DEFAULT);

  if (topic_ == nullptr)
  {
    KT_V("Error creating topic (type: '%s') '%s'", topicType, topicName);
    return false;
  }

  // Create the Subscriber
  subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr);
  if (subscriber_ == nullptr)
    return false;

  // Create the DataReader
  KT_V("Creating reader");
#ifdef DDS_RELIABLE
  DataReaderQos  rqos = DATAREADER_QOS_DEFAULT;

  rqos.reliability().kind = eprosima::fastdds::dds::BEST_EFFORT_RELIABILITY_QOS;
  rqos.durability().kind  = eprosima::fastdds::dds::VOLATILE_DURABILITY_QOS;
//  rqos.history().kind     = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
//  rqos.history().depth    = 5;
  reader_                 = subscriber_->create_datareader(topic_, rqos, &listener_);
#else
  reader_ = subscriber_->create_datareader(topic_, DATAREADER_QOS_DEFAULT, &listener_);
#endif

  if (reader_ == nullptr)
  {
    KT_E("Error creating DataReader");
    return false;
  }

  listener_.topicName_ = topicName;

  KT_V("Created reader");
  KT_V("Init done");

  return true;
}



// -----------------------------------------------------------------------------
//
// NgsildSubscriber::run -
//
void NgsildSubscriber::run(void)
{
  KT_V("Awaiting notifications");
  while (1)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100000));
  }
}
