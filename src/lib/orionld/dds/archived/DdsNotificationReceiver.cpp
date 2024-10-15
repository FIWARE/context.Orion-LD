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

extern "C"
{
#include "ktrace/kTrace.h"                                     // trace messages - ktrace library
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, ...
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjClone.h"                                     // kjClone
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // Trace levels
#include "orionld/dds/config.h"                                // DDS_RELIABLE, ...
#include "orionld/dds/kjTreeLog.h"                             // kjTreeLog2
#include "orionld/dds/NgsildEntity.h"                          // NgsildEntity
#include "orionld/dds/DdsNotificationReceiver.h"               // The class

using namespace eprosima::fastdds::dds;



// -----------------------------------------------------------------------------
//
// on_subscription_matched -
//
void DdsNotificationReceiver::on_subscription_matched(DataReader*, const SubscriptionMatchedStatus& info)
{
  if (info.current_count_change == 1)
    KT_T(StDds, "Subscriber matched.");
  else if (info.current_count_change == -1)
    KT_T(StDds, "Subscriber unmatched.");
  else
    KT_T(StDds, "'%d' is not a valid value for SubscriptionMatchedStatus current count change", info.current_count_change);
}



// -----------------------------------------------------------------------------
//
// on_data_available -
//
void DdsNotificationReceiver::on_data_available(DataReader* reader)
{
  SampleInfo info;

  KT_T(StDds, "Notification arrived");

  if (reader->take_next_sample(&ngsildEntity_, &info) == ReturnCode_t::RETCODE_OK)
  {
    if (info.valid_data)
    {
      samples_++;

      //
      // This is "more or less" how it should work:
      //   KjNode* entityP = kjEntityFromDds(&ngsildEntity_);
      //   notificationReceived(entityP);
      // The callback 'notificationReceived' is set in some constructor or init() method
      //
      KT_T(StDds, "Entity Id: %s with type: %s RECEIVED.", ngsildEntity_.id().c_str(), ngsildEntity_.type().c_str());

      //
      // Accumulate notifications
      //
      KjNode* notification = kjObject(NULL, "item");  // No name as it is part of an array
      KjNode* tenantP      = (ngsildEntity_.tenant()     != "")? kjString(NULL,   "tenant",     ngsildEntity_.tenant().c_str()) : NULL;
      KjNode* scopeP       = (ngsildEntity_.scope()      != "")? kjString(NULL,   "scope",      ngsildEntity_.scope().c_str())  : NULL;
      KjNode* createdAtP   = (ngsildEntity_.createdAt()  != 0)?  kjInteger(NULL,  "createdAt",  ngsildEntity_.createdAt())      : NULL;
      KjNode* modifiedAtP  = (ngsildEntity_.modifiedAt() != 0)?  kjInteger(NULL,  "modifiedAt", ngsildEntity_.modifiedAt())     : NULL;
      char*   attributes   = (ngsildEntity_.attributes() != "")? (char*) ngsildEntity_.attributes().c_str()                     : NULL;

      KT_T(StDds, "notification: %p", notification);
      KT_T(StDds, "topic/attr:   %s", topicName_.c_str());
      KT_T(StDds, "tenant:       %s", ngsildEntity_.tenant().c_str());
      KT_T(StDds, "entityId:     %s", ngsildEntity_.id().c_str());
      KT_T(StDds, "entityType:   %s", ngsildEntity_.type().c_str());
      KT_T(StDds, "scope:        %s", ngsildEntity_.scope().c_str());
      KT_T(StDds, "attributes:   %s", ngsildEntity_.attributes().c_str());
      KT_T(StDds, "createdAt:    %f", ngsildEntity_.createdAt());
      KT_T(StDds, "modifiedAt:   %f", ngsildEntity_.modifiedAt());

      if (tenantP     != NULL)  kjChildAdd(notification, tenantP);
      if (scopeP      != NULL)  kjChildAdd(notification, scopeP);
      if (createdAtP  != NULL)  kjChildAdd(notification, createdAtP);
      if (modifiedAtP != NULL)  kjChildAdd(notification, modifiedAtP);

      // Initializing orionldState, to call kjParse (not really necessary, it's a bit overkill)
      orionldStateInit(NULL);
      if (attributes != NULL)
      {
        KjNode* attributesP = kjParse(orionldState.kjsonP, attributes);
        attributesP = kjClone(NULL, attributesP);
        attributesP->name = (char*) "attributeValue";
        kjChildAdd(notification, attributesP);
      }

      if (callback_ != NULL)
      {
        KT_T(StDds, "Calling notification callback function");
        callback_(ngsildEntity_.type().c_str(), ngsildEntity_.id().c_str(), topicName_.c_str(), notification);
      }
      else
        KT_W("No notification callback function!");
    }
  }
}
