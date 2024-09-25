#ifndef TEST_FUNCTIONALTEST_FTCLIENT_NGSILDPUBLISHER_H_
#define TEST_FUNCTIONALTEST_FTCLIENT_NGSILDPUBLISHER_H_

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

#include <chrono>
#include <thread>
#include <unistd.h>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
}

#include "orionld/common/traceLevels.h"                     // Trace Levels

#include "ftClient/NgsildSamplePubSubTypes.hpp"
#include "ftClient/NgsildSample.hpp"
#include "ftClient/DdsNotificationSender.h"              // DdsNotificationSender

using namespace eprosima::fastdds::dds;



// -----------------------------------------------------------------------------
//
// NgsildPublisher -
//
class NgsildPublisher  // : DataWriterListener
{
 private:
  NgsildSample          entity_;
  DomainParticipant*    participant_;
  Publisher*            publisher_;
  Topic*                topic_;
  DataWriter*           writer_;
  TypeSupport           type_;
  DdsNotificationSender listener_;

 public:
  explicit NgsildPublisher(const char* topicType)
    : participant_(nullptr)
    , publisher_(nullptr)
    , topic_(nullptr)
    , writer_(nullptr)
    , type_(new NgsildSamplePubSubType())
  {
  }

  virtual ~NgsildPublisher();
  bool     init(const char* topicName);
  bool     publish(const char* entityType, const char* entityId, const char* topicName, const char* s, int i, double f, bool b);
};

#endif  // TEST_FUNCTIONALTEST_FTCLIENT_NGSILDPUBLISHER_H_
