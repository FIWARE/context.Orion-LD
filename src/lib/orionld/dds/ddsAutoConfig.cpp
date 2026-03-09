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
* Author: Marco Pistone
*/
#include <stdio.h>                                          // fprintf, fopen, fclose, snprintf
#include <stdlib.h>                                         // getpid
#include <unistd.h>                                         // sleep, unlink
#include <string.h>                                         // strcmp
#include <stdint.h>                                         // uint32_t, uint64_t, int64_t
#include <time.h>                                           // time_t, time, strftime
#include <memory>                                           // std::unique_ptr
#include <mutex>                                            // std::mutex, std::lock_guard
#include <string>                                           // std::string
#include <vector>                                           // std::vector

#include "ddsenabler/dds_enabler_runner.hpp"                // create_dds_enabler, DDSEnabler
#include "ddsenabler_participants/rpc/RpcTypes.hpp"         // eprosima::ddsenabler::participants::UUID
#include "ddsenabler_participants/Callbacks.hpp"            // TopicInfo, ServiceInfo, ActionInfo

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
}

#include "orionld/common/traceLevels.h"                     // KT_T trace levels
#include "orionld/dds/ddsCategoryToKlogSeverity.h"          // ddsCategoryToKlogSeverity
#include "orionld/dds/ddsAutoConfig.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// DiscoveredTopic - internal struct for a topic found during discovery
//
typedef struct DiscoveredTopic
{
  std::string name;
  std::string typeName;
} DiscoveredTopic;

static std::vector<DiscoveredTopic>  acDiscoveredTopics;
static std::mutex                    acMutex;



// -----------------------------------------------------------------------------
//
// acTypeNotification - no-op during auto-config discovery phase
//
static void acTypeNotification
(
  const char*           typeName,
  const char*           serializedType,
  const unsigned char*  serializedTypeInternal,
  uint32_t              serializedTypeInternalSize,
  const char*           dataPlaceholder
)
{
  KT_T(StDds, "[AutoConfig] Type discovered: '%s'", typeName);
}



// -----------------------------------------------------------------------------
//
// acTopicNotification - collect discovered topics during auto-config
//
static void acTopicNotification
(
  const char*                                           topicName,
  const eprosima::ddsenabler::participants::TopicInfo&  topicInfo
)
{
  std::lock_guard<std::mutex> lock(acMutex);

  // skip topics that are already in the list
  for (const DiscoveredTopic& dt : acDiscoveredTopics)
    if (dt.name == topicName)
      return;

  DiscoveredTopic dt;
  dt.name     = topicName;
  dt.typeName = topicInfo.type_name;
  acDiscoveredTopics.push_back(dt);

  KT_T(StDds, "[AutoConfig] Topic discovered: '%s' (type: '%s')", topicName, topicInfo.type_name.c_str());
}



// -----------------------------------------------------------------------------
//
// acNotification - no-op: we don't process data samples during discovery
//
static void acNotification(const char* topicName, const char* json, int64_t publishTime)
{
  KT_T(StDds, "[AutoConfig] Data sample on '%s' ignored (discovery phase)", topicName);
}



// -----------------------------------------------------------------------------
//
// acTypeQuery - no-op query stub
//
static bool acTypeQuery
(
  const char*                              typeName,
  std::unique_ptr<const unsigned char[]>&  serializedTypeInternal,
  uint32_t&                                serializedTypeInternalSize
)
{
  return true;
}



// -----------------------------------------------------------------------------
//
// acTopicQuery - no-op query stub
//
static bool acTopicQuery
(
  const char*                                            topicName,
  eprosima::ddsenabler::participants::TopicInfo&         topicInfo
)
{
  return true;
}



// -----------------------------------------------------------------------------
//
// acServiceNotification - no-op during discovery
//
static void acServiceNotification
(
  const char*                                              serviceName,
  const eprosima::ddsenabler::participants::ServiceInfo&   serviceInfo
)
{
  KT_T(StDdsService, "[AutoConfig] Service discovered: '%s'", serviceName);
}



// -----------------------------------------------------------------------------
//
// acServiceRequestNotification - no-op
//
static void acServiceRequestNotification
(
  const char*  serviceName,
  const char*  json,
  uint64_t     requestId,
  int64_t      publishTime
)
{
}



// -----------------------------------------------------------------------------
//
// acServiceReplyNotification - no-op
//
static void acServiceReplyNotification
(
  const char*  serviceName,
  const char*  json,
  uint64_t     requestId,
  int64_t      publishTime
)
{
}



// -----------------------------------------------------------------------------
//
// acActionNotification - no-op
//
static void acActionNotification
(
  const char*                                             actionName,
  const eprosima::ddsenabler::participants::ActionInfo&   actionInfo
)
{
}



// -----------------------------------------------------------------------------
//
// acActionGoalRequestNotification - no-op
//
static bool acActionGoalRequestNotification
(
  const char*                                           actionName,
  const char*                                           json,
  const eprosima::ddsenabler::participants::UUID&       goalId,
  int64_t                                               publishTime
)
{
  return false;
}



// -----------------------------------------------------------------------------
//
// acActionFeedbackNotification - no-op
//
static void acActionFeedbackNotification
(
  const char*                                           actionName,
  const char*                                           json,
  const eprosima::ddsenabler::participants::UUID&       goalId,
  int64_t                                               publishTime
)
{
}



// -----------------------------------------------------------------------------
//
// acActionCancelRequestNotification - no-op
//
static void acActionCancelRequestNotification
(
  const char*                                           actionName,
  const eprosima::ddsenabler::participants::UUID&       goalId,
  int64_t                                               timestamp,
  uint64_t                                              requestId,
  int64_t                                               publishTime
)
{
}



// -----------------------------------------------------------------------------
//
// acActionResultNotification - no-op
//
static void acActionResultNotification
(
  const char*                                           actionName,
  const char*                                           json,
  const eprosima::ddsenabler::participants::UUID&       goalId,
  int64_t                                               publishTime
)
{
}



// -----------------------------------------------------------------------------
//
// acActionStatusNotification - no-op
//
static void acActionStatusNotification
(
  const char*                                                 actionName,
  const eprosima::ddsenabler::participants::UUID&             goalId,
  eprosima::ddsenabler::participants::StatusCode              statusCode,
  const char*                                                 statusMessage,
  int64_t                                                     publishTime
)
{
}



// -----------------------------------------------------------------------------
//
// acActionQuery - no-op
//
static bool acActionQuery
(
  const char*                                             actionName,
  eprosima::ddsenabler::participants::ActionInfo&         actionInfo
)
{
  return false;
}



// -----------------------------------------------------------------------------
//
// acLog - minimal log forwarder
//
static void acLog(const char* fileName, int lineNo, const char* funcName, int category, const char* msg)
{
  char* filename = (fileName != NULL)? (char*) fileName : (char*) "no-filename";
  char* funcname = (funcName != NULL)? (char*) funcName : (char*) "no-funcname";
  int   level    = 0;
  char  severity = ddsCategoryToKlogSeverity(category, &level);

  ktOut(filename, lineNo, funcname, severity, level, msg);
}



// -----------------------------------------------------------------------------
//
// acWriteTempConfig - write a wildcard YAML to a temp file used during discovery
//
// Returns the path of the temp file on success (caller must free it), or NULL.
//
static char* acWriteTempConfig(void)
{
  char* tmpPath = (char*) malloc(64);
  if (tmpPath == NULL)
  {
    KT_E("malloc failed for temp config path");
    return NULL;
  }

  snprintf(tmpPath, 64, "/tmp/dds_autoconf_%d.yaml", (int) getpid());

  FILE* fp = fopen(tmpPath, "w");
  if (fp == NULL)
  {
    KT_E("Cannot create temp DDS config file '%s'", tmpPath);
    free(tmpPath);
    return NULL;
  }

  fprintf(fp,
    "# Temporary DDS configuration - auto-discovery phase\n"
    "dds:\n"
    "  domain: 0\n"
    "\n"
    "  allowlist:\n"
    "    - name: \"*\"\n"
    "\n"
    "  blocklist: []\n"
    "\n"
    "topics:\n"
    "  name: \"*\"\n"
    "  qos:\n"
    "    durability: TRANSIENT_LOCAL\n"
    "    history-depth: 5\n"
    "\n"
    "ddsenabler:\n"
    "\n"
    "specs:\n"
    "  threads: 4\n"
    "  logging:\n"
    "    stdout: false\n"
    "    verbosity: warning\n"
  );

  fclose(fp);
  return tmpPath;
}



// -----------------------------------------------------------------------------
//
// acWriteOutputConfig - serialize discovered topics into the output YAML file
//
static int acWriteOutputConfig(const char* outConfigFile)
{
  FILE* fp = fopen(outConfigFile, "w");
  if (fp == NULL)
  {
    KT_E("Cannot open output DDS config file '%s' for writing", outConfigFile);
    return -1;
  }

  // Timestamp header
  time_t  now = time(NULL);
  char    ts[64];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

  fprintf(fp,
    "# DDS configuration - auto-generated by Orion-LD DDS autodiscovery\n"
    "# Generated at: %s\n"
    "dds:\n"
    "  domain: 0\n"
    "\n",
    ts
  );

  std::lock_guard<std::mutex> lock(acMutex);

  if (acDiscoveredTopics.empty())
  {
    KT_W("[AutoConfig] No DDS topics discovered - using wildcard allowlist");
    fprintf(fp,
      "  allowlist:\n"
      "    - name: \"*\"\n"
      "\n"
    );
  }
  else
  {
    fprintf(fp, "  allowlist:\n");
    for (const DiscoveredTopic& dt : acDiscoveredTopics)
      fprintf(fp, "    - name: \"%s\"\n", dt.name.c_str());
    fprintf(fp, "\n");
  }

  fprintf(fp,
    "  blocklist: []\n"
    "\n"
    "topics:\n"
    "  name: \"*\"\n"
    "  qos:\n"
    "    durability: TRANSIENT_LOCAL\n"
    "    history-depth: 5\n"
    "\n"
    "# DDS Enabler configuration\n"
    "ddsenabler:\n"
    "\n"
    "#Specs configuration\n"
    "specs:\n"
    "  threads: 12\n"
    "  logging:\n"
    "    stdout: false\n"
    "    verbosity: info\n"
  );

  fclose(fp);

  KT_T(StDds, "[AutoConfig] Wrote %zu topic(s) to '%s'", acDiscoveredTopics.size(), outConfigFile);
  return 0;
}



// -----------------------------------------------------------------------------
//
// ddsAutoConfig - entry point
//
// Starts a temporary DDSEnabler with wildcard allowlist, waits
// `discoveryWindowSecs` seconds to collect topic announcements via DDS
// participant discovery, then writes the discovered topics into `outConfigFile`
// (YAML format compatible with DDS_ENABLER_CONFIGURATION.yaml).
//
int ddsAutoConfig(const char* outConfigFile, int discoveryWindowSecs)
{
  KT_T(StDds, "[AutoConfig] Starting DDS discovery (window: %d seconds)", discoveryWindowSecs);

  // 1. Reset collected topics from any previous run
  {
    std::lock_guard<std::mutex> lock(acMutex);
    acDiscoveredTopics.clear();
  }

  // 2. Write temporary wildcard YAML config used only during discovery
  char* tmpConfigPath = acWriteTempConfig();
  if (tmpConfigPath == NULL)
    return -1;

  // 3. Build callback set
  eprosima::ddsenabler::DdsCallbacks ddsCallbacks =
  {
    acTypeNotification,
    acTopicNotification,
    acNotification,
    acTypeQuery,
    acTopicQuery
  };
  eprosima::ddsenabler::ServiceCallbacks svcCallbacks =
  {
    acServiceNotification,
    acServiceRequestNotification,
    acServiceReplyNotification
  };
  eprosima::ddsenabler::ActionCallbacks actCallbacks =
  {
    acActionNotification,
    acActionGoalRequestNotification,
    acActionFeedbackNotification,
    acActionCancelRequestNotification,
    acActionResultNotification,
    acActionStatusNotification,
    acActionQuery
  };
  eprosima::ddsenabler::CallbackSet callbackSet =
  {
    acLog,
    ddsCallbacks,
    svcCallbacks,
    actCallbacks
  };

  // 4. Start the discovery enabler
  std::shared_ptr<eprosima::ddsenabler::DDSEnabler> discoveryEnabler;
  bool ok = eprosima::ddsenabler::create_dds_enabler(tmpConfigPath, callbackSet, discoveryEnabler);

  unlink(tmpConfigPath);   // temp file no longer needed
  free(tmpConfigPath);

  if (!ok)
  {
    KT_E("[AutoConfig] Failed to create discovery DDSEnabler");
    return -1;
  }

  KT_T(StDds, "[AutoConfig] Discovery enabler running - waiting %d seconds ...", discoveryWindowSecs);

  // 5. Wait for the discovery window to elapse
  sleep((unsigned int) discoveryWindowSecs);

  // 6. Destroy the discovery enabler before writing the config
  discoveryEnabler.reset();
  KT_T(StDds, "[AutoConfig] Discovery phase complete - %zu topic(s) found", acDiscoveredTopics.size());

  // 7. Write the output YAML
  int rc = acWriteOutputConfig(outConfigFile);
  return rc;
}
