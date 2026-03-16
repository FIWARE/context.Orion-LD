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
* Author: Ken Zangelin
*/
#include <unistd.h>                                         // access
#include <stdlib.h>                                         // malloc, free
#include <memory>                                           // for std::unique_ptr
#include <string>                                           // for std::string

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler
#include "ddsenabler_participants/rpc/RpcTypes.hpp"         // eprosima::ddsenabler::participants::UUID

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
}

#include "orionld/types/DdsType.h"                          // DdsType
#include "orionld/common/traceLevels.h"                     // Trace levels for KTrace
#include "orionld/common/orionldState.h"                    // configFile, configTree, ddsServices
#include "orionld/config/configDdsTopicToAttribute.h"       // configDdsTopicToAttribute
#include "orionld/kjTree/kjTreeNavigate.h"                  // kjTreeNavigate
#include "orionld/dds/ddsPrePopulateDb.h"                   // ddsPrePopulateDb, DdsConceptType
#include "orionld/dds/ddsServiceList.h"                     // ddsServiceList
#include "orionld/dds/ddsTypes.h"                           // ddsTypeNotification, ddsTypeLookup
#include "orionld/dds/ddsNotification.h"                    // ddsNotification
#include "orionld/dds/ddsTopicNotification.h"               // ddsTopicNotification
#include "orionld/dds/ddsServiceNotification.h"             // ddsServiceNotification
#include "orionld/dds/ddsServiceReplyNotification.h"        // ddsServiceReplyNotification
#include "orionld/dds/ddsActionNotification.h"              // ddsActionNotification
#include "orionld/dds/ddsActionResultNotification.h"        // ddsActionResultNotification
#include "orionld/dds/ddsActionFeedbackNotification.h"      // ddsActionFeedbackNotification
#include "orionld/dds/ddsActionStatusNotification.h"        // ddsActionStatusNotification
#include "orionld/dds/ddsCategoryToKlogSeverity.h"          // ddsCategoryToKlogSeverity
#include "orionld/dds/ddsInit.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// ddsEnabler -
//
std::shared_ptr<eprosima::ddsenabler::DDSEnabler>  ddsEnabler;



// -----------------------------------------------------------------------------
//
// ddsTypeQuery -
//
static bool ddsTypeQuery  // DdsTypeQuery
(
  const char*                              typeName,
  std::unique_ptr<const unsigned char[]>&  serializedTypeInternal,
  uint32_t&                                serializedTypeInternalSize
)
{
  KT_T(StDdsTypes, "Got a type query/request callback ('%s', %d)", typeName, serializedTypeInternalSize);
  return true;;
}



// -----------------------------------------------------------------------------
//
// ddsTopicQuery -
//
static bool ddsTopicQuery(const char* topicName, eprosima::ddsenabler::participants::TopicInfo& topicInfo)
{
  KT_T(StDds, "Got a topic query callback ('%s', '%s', '%s')", topicName, topicInfo.type_name, topicInfo.serialized_qos.c_str());

  // char* entityId      = NULL;
  // char* entityType    = NULL;
  // char* attrShortName = configDdsTopicToAttribute(topicName, &entityId, &entityType);

  // if (attrShortName == NULL)
  //   typeName = "";
  // else, look up the ddsTypeName of the attribute IN MONGO !!!   Better add it to config file in ddsTopicNotification
  return true;
}



// -----------------------------------------------------------------------------
//
// ddsLog -
//
static void ddsLog(const char* fileName, int lineNo, const char* funcName, int category, const char* msg)
{
  char* filename = (fileName != NULL)? (char*) fileName : (char*) "no-filename";
  char* funcname = (funcName != NULL)? (char*) funcName : (char*) "no-funcname";
  int   level    = 0;
  char  severity = ddsCategoryToKlogSeverity(category, &level);

  ktOut(filename, lineNo, funcname,  severity, level, msg);
}



// -----------------------------------------------------------------------------
//
// ddsServiceRequestNotification -
//
void ddsServiceRequestNotification
(
  const char* serviceName,
  const char* json,
  uint64_t    requestId,
  int64_t     publishTime
)
{
  KT_T(StDdsService, "Got a Service Request Notification (action: '%s', req: %lld): '%s'", serviceName, requestId, json);
}




// -----------------------------------------------------------------------------
//
// ddsActionGoalRequestNotification -
//
bool ddsActionGoalRequestNotification
(
  const char*                                      actionName,
  const char*                                      json,
  const eprosima::ddsenabler::participants::UUID&  goalId,
  int64_t                                          publishTime
)
{
  KT_T(StDdsAction, "Got an Action Goal Request Notification (action: '%s'): '%s'", actionName, json);
  return false;
}



// -----------------------------------------------------------------------------
//
// ddsActionCancelRequestNotification -
//
void ddsActionCancelRequestNotification
(
  const char*                                     actionName,
  const eprosima::ddsenabler::participants::UUID& goalId,
  int64_t                                         timestamp,
  uint64_t                                        requestId,
  int64_t                                         publishTime
)
{
  KT_T(StDdsAction, "Got an Action Cancel Request Notification (action: %s)", actionName);
}



// -----------------------------------------------------------------------------
//
// ddsActionQuery -
//
bool ddsActionQuery
(
  const char*                                     actionName,
  eprosima::ddsenabler::participants::ActionInfo& actionInfo
)
{
  KT_T(StDdsAction, "Got an Action Query (action: %s)", actionName);
  return false;
}



// -----------------------------------------------------------------------------
//
// ddsInit - initialization function for DDS
//
// PARAMETERS
// * mode - the DDS mode the broker is working in
//
int ddsInit(Kjson* kjP)
{
  KjNode* topicsNode   = kjTreeNavigate(configTree, "dds.ngsild.topics",   NULL);
  KjNode* servicesNode = kjTreeNavigate(configTree, "dds.ngsild.services", NULL);
  KjNode* actionsNode  = kjTreeNavigate(configTree, "dds.ngsild.actions",  NULL);

  if (topicsNode   != NULL)  ddsPrePopulateDb(DdsTopics,   topicsNode);
  if (servicesNode != NULL)  ddsPrePopulateDb(DdsServices, servicesNode);
  if (actionsNode  != NULL)  ddsPrePopulateDb(DdsActions,  actionsNode);

  KT_T(StDds, "Calling create_dds_enabler('%s')", configFile);

  eprosima::utils::Log::ReportFilenames(true);

  eprosima::ddsenabler::DdsCallbacks callbacks =
  {
    ddsTypeNotification,
    ddsTopicNotification,
    ddsNotification,
    ddsTypeQuery,
    ddsTopicQuery
  };
  eprosima::ddsenabler::ServiceCallbacks serviceCallbacks =
  {
    ddsServiceNotification,
    ddsServiceRequestNotification,
    ddsServiceReplyNotification
  };
  eprosima::ddsenabler::ActionCallbacks actionCallbacks =
  {
    ddsActionNotification,
    ddsActionGoalRequestNotification,
    ddsActionFeedbackNotification,
    ddsActionCancelRequestNotification,
    ddsActionResultNotification,
    ddsActionStatusNotification,
    ddsActionQuery
  };
  eprosima::ddsenabler::CallbackSet callbackSet =
  {
    ddsLog,
    callbacks,
    serviceCallbacks,
    actionCallbacks
  };


  bool r = eprosima::ddsenabler::create_dds_enabler(configFile, callbackSet, ddsEnabler);
  if (r == false)
    KT_X(1, "Unable to create the DDS Enabler");
  KT_T(StDds, "DDS Enabler created");

  return 0;
}
