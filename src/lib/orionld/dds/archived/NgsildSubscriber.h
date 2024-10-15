#ifndef SRC_LIB_ORIONLD_DDS_NGSILDSUBSCRIBER_H_
#define SRC_LIB_ORIONLD_DDS_NGSILDSUBSCRIBER_H_

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
#include <chrono>
#include <thread>

#include "fastdds/dds/domain/DomainParticipant.hpp"
#include "fastdds/dds/domain/DomainParticipantFactory.hpp"
#include "fastdds/dds/subscriber/DataReader.hpp"
#include "fastdds/dds/subscriber/DataReaderListener.hpp"
#include "fastdds/dds/subscriber/qos/DataReaderQos.hpp"
#include "fastdds/dds/subscriber/SampleInfo.hpp"
#include "fastdds/dds/subscriber/Subscriber.hpp"
#include "fastdds/dds/topic/TypeSupport.hpp"

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/kjBuilder.h"                                // kjObject, kjString, kjChildAdd, ...
#include "kjson/kjParse.h"                                  // kjParse
#include "kjson/kjClone.h"                                  // kjClone
}

#include "orionld/common/orionldState.h"                    // orionldState
#include "orionld/common/traceLevels.h"                     // Trace Levels
#include "orionld/dds/NgsildEntityPubSubTypes.h"            // DDS stuff ...
#include "orionld/dds/DdsNotificationReceiver.h"            // DdsNotificationReceiver
#include "orionld/dds/config.h"                             // DDS_RELIABLE, ...

using namespace eprosima::fastdds::dds;



// -----------------------------------------------------------------------------
//
// NgsildSubscriber -
//
// FIXME: All the implementation needs to go to NgsildSubscriber.cpp
//
class NgsildSubscriber
{
 private:
  DomainParticipant*       participant_;
  Subscriber*              subscriber_;
  DataReader*              reader_;
  Topic*                   topic_;
  TypeSupport              type_;
  DdsNotificationReceiver  listener_;

 public:
  explicit NgsildSubscriber(const char* topicType, DdsNotificationFunction callback)
    : participant_(nullptr)
    , subscriber_(nullptr)
    , reader_(nullptr)
    , topic_(nullptr)
    , type_(new NgsildEntityPubSubType(topicType))
    , listener_(callback)
  {
  }

  virtual ~NgsildSubscriber();
  bool init(const char* topicName);
  void run(void);
};

#endif  // SRC_LIB_ORIONLD_DDS_NGSILDSUBSCRIBER_H_
