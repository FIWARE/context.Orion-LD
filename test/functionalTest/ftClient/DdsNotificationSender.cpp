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
#include "ftClient/NgsildPublisher.h"                       // The class



// -----------------------------------------------------------------------------
//
// DdsNotificationSender::on_publication_matched -
//
void DdsNotificationSender::on_publication_matched(DataWriter*, const PublicationMatchedStatus& info)
{
  // FIXME: Don't Publish until entering here!  (mutex)
  KT_V("info.current_count_change: %d", info.current_count_change);
  if (info.current_count_change == 1)
  {
    matched_ = info.total_count;
    KT_T(StDds, "Publisher matched.");
    ready_ = true;
  }
  else if (info.current_count_change == -1)
  {
    matched_ = info.total_count;
    KT_T(StDds, "Publisher unmatched.");
    ready_ = false;
  }
  else
    KT_T(StDds, "'%d' is not a valid value for PublicationMatchedStatus current count change.", info.total_count);
}
