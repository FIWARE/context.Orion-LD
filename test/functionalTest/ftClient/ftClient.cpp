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
* Author: Ken Zangelin, David Campo, Luis Arturo Frigolet
*/
#include <unistd.h>                                         // sleep
#include <string.h>                                         // memcpy
#include <strings.h>                                        // bzero
#include <stdlib.h>                                         // exit, malloc, calloc, free
#include <stdarg.h>                                         // va_start, ...

#include <string>                                           // std::string
#include <memory>                                           // for std::unique_ptr

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler
#include "ddsenabler_participants/rpc/RpcTypes.hpp"         // eprosima::ddsenabler::participants::UUID

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "ktrace/ktGlobals.h"                               // globals for KT library
#include "kargs/kargs.h"                                    // argument parsing - kargs library
#include "kalloc/kaInit.h"                                  // kaInit
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjParse.h"                                  // kjParse
#include "kjson/kjBuilder.h"                                // kjObject, ...
#include "kjson/kjClone.h"                                  // kjClone
}

#include "common/orionldState.h"                            // orionldState
#include "types/Verb.h"                                     // HTTP Verbs
#include "common/traceLevels.h"                             // Trace levels for ktrace
#include "dds/ddsCategoryToKlogSeverity.h"                  // ddsCategoryToKlogSeverity
#include "ftClient/mhdInit.h"                               // mhdInit
#include "ftClient/postDdsType.h"                           // ddsTypeLookup
#include "ftClient/postDdsService.h"                        // ddsServiceInfoLookup



// -----------------------------------------------------------------------------
//
// FTCLIENT_VERSION -
//
#define FTCLIENT_VERSION "0.0.1"



// -----------------------------------------------------------------------------
//
// CLI Param variables
//
char*                traceLevels;
char*                logDir        = NULL;
char*                logLevel;
KBool                logToScreen;
KBool                fixme;
unsigned short       ldPort;
char*                httpsKey;
char*                httpsCertificate;
unsigned int         mhdPoolSize;
unsigned int         mhdMemoryLimit;
unsigned int         mhdTimeout;
unsigned int         mhdMaxConnections;
bool                 distributed;
unsigned long long   inReqPayloadMaxSize  = 64 * 1024;
char                 configFile[512];
bool                 ddsSupport       = false;
char*                ddsServiceName   = NULL;
char*                ddsActionName    = NULL;



// -----------------------------------------------------------------------------
//
// kargs - vector of CLI parameters
//
KArg kargs[] =
{
  //
  // Potential builtins
  //
  { "--trace",            "-t",     KaString,  &traceLevels,          KaOpt, 0,          KA_NL,    KA_NL,      "trace levels (csv of levels/ranges)"               },
  { "--logDir",           "-ld",    KaString,  &logDir,               KaOpt, _vp "/tmp", KA_NL,    KA_NL,      "log file directory"                                },
  { "--logLevel",         "-ll",    KaString,  &logLevel,             KaOpt, 0,          KA_NL,    KA_NL,      "log level (ERR|WARN|INFO|INFO|VERBOSE|TRACE|DEBUG" },
  { "--logToScreen",      "-ls",    KaBool,    &logToScreen,          KaOpt, KFALSE,     KA_NL,    KA_NL,      "log to screen"                                     },
  { "--fixme",            "-fix",   KaBool,    &fixme,                KaOpt, KFALSE,     KA_NL,    KA_NL,      "FIXME messages"                                    },
  { "--config",           "-cfg",   KaString,  &configFile,           KaOpt, NULL,       KA_NL,    KA_NL,      "Config File"                                       },
  { "--dds",              "-dds",   KaBool,    &ddsSupport,           KaOpt, KFALSE,     KA_NL,    KA_NL,      "DDS Support"                                       },
  { "--ddsService",       "-ddss",  KaString,  &ddsServiceName,       KaOpt, NULL,       KA_NL,    KA_NL,      "DDS Service to announce as server"                 },
  { "--ddsAction",        "-ddsa",  KaString,  &ddsActionName,        KaOpt, NULL,       KA_NL,    KA_NL,      "DDS Action to announce as server"                  },

  //
  // Broker options
  //
  { "--port",             "-p",     KaUShort,  &ldPort,               KaOpt, _vp 7701,   _vp 1027,  _vp 65535, "TCP port for incoming requests"                    },
  { "--httpsKey",         "-k",     KaString,  &httpsKey,             KaOpt, NULL,       KA_NL,    KA_NL,      "https key file"                                    },
  { "--httpsCertificate", "-c",     KaString,  &httpsCertificate,     KaOpt, NULL,       KA_NL,    KA_NL,      "https certificate file"                            },
  { "--distOps",          "-dops",  KaBool,    &distributed,          KaOpt, KFALSE,     KA_NL,    KA_NL,      "support for distributed operations"                },

  // MHD
  { "--mhdPoolSize",      "-mps",   KaUInt,    &mhdPoolSize,          KaOpt, _vp 8,      _vp 0,     _vp 1024,  "MHD request thread pool size"                      },
  { "--mhdMemoryLimit",   "-mlim",  KaUInt,    &mhdMemoryLimit,       KaOpt, _vp 64,     _vp 0,     _vp 1024,  "MHD memory limit (in kb)"                          },
  { "--mhdTimeout",       "-mtmo",  KaUInt,    &mhdTimeout,           KaOpt, _vp 2000,   _vp 0,     KA_NL,     "MHD connection timeout (in milliseconds)"          },
  { "--mhdConnections",   "-mcon",  KaUInt,    &mhdMaxConnections,    KaOpt, _vp 512,    _vp 1,     KA_NL,     "Max number of MHD connections"                     },

  KARGS_END
};








// -----------------------------------------------------------------------------
//
// klibLogBuffer -
//
char klibLogBuffer[4 * 1024];



// -----------------------------------------------------------------------------
//
// klibLogFunction -
//
static void klibLogFunction
(
  int          severity,              // 1: Error, 2: Warning, 3: Info, 4: Verbose, 5: Trace
  int          level,                 // Trace level || Error code || Info Code
  const char*  fileName,
  int          lineNo,
  const char*  functionName,
  const char*  format,
  ...
)
{
  va_list  args;

  /* "Parse" the variable arguments */
  va_start(args, format);

  /* Print message to variable */
  vsnprintf(klibLogBuffer, sizeof(klibLogBuffer), format, args);
  va_end(args);

  // LM_K(("Got a lib log message, severity: %d: %s", severity, libLogBuffer));

  if (severity == 1)
    ktOut(fileName, lineNo, functionName, 'E', 0, "klib: %s", klibLogBuffer);
  else if (severity == 2)
    ktOut(fileName, lineNo, functionName, 'W', 0, "klib: %s", klibLogBuffer);
  else if (severity == 3)
    ktOut(fileName, lineNo, functionName, 'I', 0, "klib: %s", klibLogBuffer);
  else if (severity == 4)
    ktOut(fileName, lineNo, functionName, 'V', 0, "klib: %s", klibLogBuffer);
  else if (severity == 5)
    ktOut(fileName, lineNo, functionName, 'T', level + 1000, "klib: %s", klibLogBuffer);
}



extern KjNode*  ddsDumpArray;
KjNode*         ddsServiceRequestsArray = NULL;  // Stores received DDS service requests



// -----------------------------------------------------------------------------
//
// ddsNotification -
//
static void ddsNotification(const char* topicName, const char* json, int64_t publishTime)
{
  KT_T(StDdsDump, "**********************************************************************************");
  KT_T(StDdsDump, "Got a notification on topic '%s' (json: %s)", topicName, json);
  KT_T(StDdsDump, "**********************************************************************************");

  orionldStateInit(NULL);
  KjNode* dump = kjParse(orionldState.kjsonP, (char*) json);
  KT_T(StDdsDump, "parsed the notification");

  if (dump == NULL)
    KT_E("Error parsing the incoming JSON notification");

  if (ddsDumpArray == NULL)
  {
    KT_T(StDdsDump, "Creating the DDS DumpArray");
    ddsDumpArray = kjArray(NULL, "ddsDumpArray");
  }
  dump = kjClone(NULL, dump);
  kjChildAdd(ddsDumpArray, dump);
}



// -----------------------------------------------------------------------------
//
// ddsTypeNotification -
//
static void ddsTypeNotification
(
  const char*           typeName,
  const char*           serializedType,
  const unsigned char*  serializedTypeInternal,
  uint32_t              serializedTypeInternalSize,
  const char*           dataPlaceholder
)
{
  KT_T(StDds, "----------------------------------------");
  KT_T(StDds, "Got a type notification:");
  KT_T(StDds, "o typeName:                    %s", typeName);
  KT_T(StDds, "o serializedType:              %s", serializedType);
  KT_T(StDds, "o serializedTypeInternal:      %s", serializedTypeInternal);
  KT_T(StDds, "o serializedTypeInternalSize:  %d", serializedTypeInternalSize);
  KT_T(StDds, "o dataPlaceholder:             %s", dataPlaceholder);              // NOTE: use this field for 'DDS Type Awareness'
  KT_T(StDds, "Nothing done, for now at least");
  KT_T(StDds, "----------------------------------------");
}



// -----------------------------------------------------------------------------
//
// ddsTopicNotification -
//
void ddsTopicNotification(const char* topicName, const eprosima::ddsenabler::participants::TopicInfo& topicInfo)
{
  KT_T(StDds, "Got a topic notification ('%s', '%s', '%s')", topicName, topicInfo.type_name, topicInfo.serialized_qos);
}



// -----------------------------------------------------------------------------
//
// ddsTypeRequest -
//
static bool ddsTypeRequest
(
  const char*                             typeName,
  std::unique_ptr<const unsigned char[]>& serializedTypeInternal,
  uint32_t&                               serializedTypeInternalSize
)
{
  KT_T(StDds, "Got a type request callback ('%s')", typeName);

  //
  // Load the type file using eProsima's safe filename convention (type_name with ':' -> '_').
  // This avoids the ambiguity of reversing filenames back to type names
  // (e.g. "dds_::X" and "dds::_X" both produce "dds___X" as filename).
  //
  unsigned char*  data = NULL;
  uint32_t        size = 0;

  if (ddsTypeLoadByName(typeName, &data, &size) == false)
  {
    KT_T(StDds, "Type '%s' not found via file lookup", typeName);
    return false;
  }

  // Transfer ownership to unique_ptr (DDS Enabler takes ownership)
  serializedTypeInternal.reset(data);
  serializedTypeInternalSize = size;

  KT_T(StDds, "Returning type '%s' (%u bytes)", typeName, size);
  return true;
}



// -----------------------------------------------------------------------------
//
// ddsTopicQuery -
//
static bool ddsTopicQuery(const char* topicName, eprosima::ddsenabler::participants::TopicInfo& topicInfo)
{
  KT_T(StDds, "Got a Topic Query callback (topic: '%s', type: '%s')", topicName, topicInfo.type_name.c_str());
  return true;
}



// -----------------------------------------------------------------------------
//
// ddsLog -
//
static void ddsLog(const char* fileName, int lineNo, const char* funcName, int category, const char* msg)
{
  int  level    = 0;
  char severity = ddsCategoryToKlogSeverity(category, &level);

  char* filename = (fileName != NULL)? (char*) fileName : (char*) "no-filename";
  char* funcname = (funcName != NULL)? (char*) funcName : (char*) "no-funcname";

  ktOut(filename, lineNo, funcname,  severity, level, msg);
}


// -----------------------------------------------------------------------------
//
// ddsServiceNotification -
//
static void ddsServiceNotification(const char* serviceName, const eprosima::ddsenabler::participants::ServiceInfo& serviceInfo)
{
  KT_T(StDds, "Got a Service Notification (service: %s)", serviceName);
}



// -----------------------------------------------------------------------------
//
// ddsServiceQuery -
//
// This callback provides service type information when announcing a service.
// It looks up the service info from the map populated by POST /dds/service.
//
static bool ddsServiceQuery(const char* serviceName, eprosima::ddsenabler::participants::ServiceInfo& serviceInfo)
{
  KT_T(StDds, "Got a Service Query for '%s'", serviceName);

  DdsServiceInfo* info = ddsServiceInfoLookup(serviceName);
  if (info != NULL)
  {
    serviceInfo.request.type_name = info->requestType;
    serviceInfo.request.serialized_qos = info->requestQos;
    serviceInfo.reply.type_name = info->replyType;
    serviceInfo.reply.serialized_qos = info->replyQos;
  }
  else
  {
    // Fallback to placeholder if no info stored
    serviceInfo.request.type_name = std::string(serviceName) + "_Request";
    serviceInfo.request.serialized_qos = "";
    serviceInfo.reply.type_name = std::string(serviceName) + "_Reply";
    serviceInfo.reply.serialized_qos = "";
  }

  KT_T(StDds, "Returning service info for '%s': req='%s', reply='%s'",
       serviceName, serviceInfo.request.type_name.c_str(), serviceInfo.reply.type_name.c_str());

  return true;
}



// -----------------------------------------------------------------------------
//
// ddsServiceRequestNotification -
//
static void ddsServiceRequestNotification
(
  const char* serviceName,
  const char* json,
  uint64_t    requestId,
  int64_t     publishTime
)
{
  KT_T(StDds, "Got a Service Request Notification (service: '%s', reqId: %llu): '%s'", serviceName, requestId, json);

  // Store the request for later retrieval via REST
  orionldStateInit(NULL);

  KjNode* request = kjObject(orionldState.kjsonP, NULL);
  KjNode* serviceP = kjString(orionldState.kjsonP, "service", serviceName);
  KjNode* reqIdP = kjInteger(orionldState.kjsonP, "requestId", requestId);
  KjNode* timeP = kjInteger(orionldState.kjsonP, "publishTime", publishTime);
  KjNode* bodyP = kjParse(orionldState.kjsonP, (char*) json);

  kjChildAdd(request, serviceP);
  kjChildAdd(request, reqIdP);
  kjChildAdd(request, timeP);
  if (bodyP != NULL)
  {
    bodyP->name = (char*) "body";
    kjChildAdd(request, bodyP);
  }

  if (ddsServiceRequestsArray == NULL)
    ddsServiceRequestsArray = kjArray(NULL, "ddsServiceRequests");

  request = kjClone(NULL, request);
  kjChildAdd(ddsServiceRequestsArray, request);
}



// -----------------------------------------------------------------------------
//
// ddsServiceReplyNotification -
//
static void ddsServiceReplyNotification
(
  const char* serviceName,
  const char* json,
  uint64_t    requestId,
  int64_t     publishTime
)
{
  KT_T(StDds, "Got a Service Reply Notification (action: '%s', req: %lld): '%s'", serviceName, requestId, json);
}



// -----------------------------------------------------------------------------
//
// ddsActionNotification -
//
static void ddsActionNotification(const char* actionName, const eprosima::ddsenabler::participants::ActionInfo& actionInfo)
{
  KT_T(StDds, "Got an Action Notification (action: %s)", actionName);
}



// -----------------------------------------------------------------------------
//
// ddsActionGoalRequestNotification -
//
static bool ddsActionGoalRequestNotification
(
  const char* actionName,
  const char* json,
  const eprosima::ddsenabler::participants::UUID& goalId,
  int64_t     publishTime
)
{
  KT_T(StDds, "Got an Action Goal Request Notification (action: '%s'): '%s'", actionName, json);
  return false;
}



// -----------------------------------------------------------------------------
//
// ddsActionFeedbackNotification -
//
static void ddsActionFeedbackNotification
(
  const char* actionName,
  const char* json,
  const eprosima::ddsenabler::participants::UUID& goalId,
  int64_t     publishTime
)
{
  KT_T(StDds, "Got an Action Goal Request Notification (action: '%s'): '%s'", actionName, json);
}



// -----------------------------------------------------------------------------
//
// ddsActionCancelRequestNotification -
//
static void ddsActionCancelRequestNotification
(
  const char* actionName,
  const eprosima::ddsenabler::participants::UUID& goalId,
  int64_t     timestamp,
  uint64_t    requestId,
  int64_t     publishTime
)
{
  KT_T(StDds, "Got an Action Cancel Request Notification (action: %s)", actionName);
}



// -----------------------------------------------------------------------------
//
// ddsActionResultNotification -
//
static void ddsActionResultNotification
(
  const char* actionName,
  const char* json,
  const eprosima::ddsenabler::participants::UUID& goalId,
  int64_t     publishTime
)
{
  KT_T(StDds, "Got an Action Result Notification (action: '%s'): '%s'", actionName, json);
}



// -----------------------------------------------------------------------------
//
// ddsActionStatusNotification -
//
static void ddsActionStatusNotification
(
  const char*  actionName,
  const eprosima::ddsenabler::participants::UUID&  goalId,
  eprosima::ddsenabler::participants::StatusCode   statusCode,
  const char*  statusMessage,
  int64_t      publishTime
)
{
  KT_T(StDds, "Got an Action Status Notification (action: %s, status %d): %s", actionName, statusCode, statusMessage);
}



// -----------------------------------------------------------------------------
//
// ddsActionQuery -
//
static bool ddsActionQuery
(
  const char* actionName,
  eprosima::ddsenabler::participants::ActionInfo& actionInfo
)
{
  KT_T(StDds, "Got an Action Query (action: %s)", actionName);
  return false;
}



std::shared_ptr<eprosima::ddsenabler::DDSEnabler> ddsEnabler;
// -----------------------------------------------------------------------------
//
// main -
//
int main(int argC, char* argV[])
{
  KArgsStatus ks;
  const char* progName = "ftClient";
  char        configFilePath[256];

  ks = kargsInit(progName, kargs, "FTCLIENT");
  if (ks != KargsOk)
  {
    fprintf(stderr, "error reading CLI parameters\n");
    exit(1);
  }

  ks = kargsParse(argC, argV);
  if (ks != KargsOk)
  {
    kargsUsage();
    exit(1);
  }

  // Config file
  char* configFileP = (configFile[0] == 0)? NULL : configFile;
  if (configFile[0] == 0)
  {
    char* home = getenv("HOME");
    if (home != NULL)
    {
      snprintf(configFilePath, sizeof(configFilePath) - 1, "%s/.ftClient", home);
      configFileP = configFilePath;
    }
  }

  if (configFileP != NULL)
    configFileP = strdup(configFileP);

  int kt = ktInit(progName, logDir, logToScreen, logLevel, traceLevels, kaBuiltinVerbose, kaBuiltinDebug, fixme);

  if (kt != 0)
  {
    fprintf(stderr, "Error initializing logging library\n");
    exit(1);
  }

  kaInit(klibLogFunction);


  //
  // Perhaps the most important feature of ftClient is the ability to report on received notifications.
  // For this purpose, the dumpArray contains all payloads received as notifications.
  //
  // NOTE: not only notifications, also forwarded requests, or just about anything received out of the defined API it supports for
  //       configuration.
  //
  KT_D("%s version: %s", progName, FTCLIENT_VERSION);

  mhdInit(ldPort);

  if (ddsSupport == true)
  {
    eprosima::utils::Log::ReportFilenames(true);

    eprosima::ddsenabler::DdsCallbacks callbacks =
    {
      ddsTypeNotification,
      ddsTopicNotification,
      ddsNotification,
      ddsTypeRequest,
      ddsTopicQuery
    };
    eprosima::ddsenabler::ServiceCallbacks serviceCallbacks =
    {
      ddsServiceNotification,
      ddsServiceRequestNotification,
      ddsServiceReplyNotification,
      ddsServiceQuery
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

    bool r = eprosima::ddsenabler::create_dds_enabler(configFileP, callbackSet, ddsEnabler);
    if (r == false)
      KT_X(1, "Unable to create the DDS Enabler");

    KT_D("DDS Enabler created");

    // Announce as DDS service server if service name is provided
    if (ddsServiceName != NULL)
    {
      KT_D("Announcing DDS service '%s'", ddsServiceName);
      if (ddsEnabler->announce_service(ddsServiceName) == false)
        KT_E("Failed to announce DDS service '%s'", ddsServiceName);
      else
        KT_D("Successfully announced DDS service '%s'", ddsServiceName);
    }

    // Announce as DDS action server if action name is provided
    if (ddsActionName != NULL)
    {
      KT_D("Announcing DDS action '%s'", ddsActionName);
      if (ddsEnabler->announce_action(ddsActionName) == false)
        KT_E("Failed to announce DDS action '%s'", ddsActionName);
      else
        KT_D("Successfully announced DDS action '%s'", ddsActionName);
    }
  }

  while (1)
  {
    sleep(1);
  }
}
