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
#include <strings.h>                                        // bzero
#include <stdlib.h>                                         // exit, malloc, calloc, free
#include <stdarg.h>                                         // va_start, ...

#include <string>                                           // std::string
#include <memory>                                           // for std::unique_ptr

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler

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



extern KjNode* ddsDumpArray;
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
  KT_T(StDds, "o dataPlaceholder:             %s", dataPlaceholder);
  KT_T(StDds, "Nothing done, for now at least");
  KT_T(StDds, "----------------------------------------");
}



// -----------------------------------------------------------------------------
//
// ddsTopicNotification -
//
void ddsTopicNotification(const char* topicName, const char* typeName, const char* serializedQos)
{
  KT_T(StDds, "Got a topic notification ('%s', '%s', '%s')", topicName, typeName, serializedQos);
}



// -----------------------------------------------------------------------------
//
// ddsTypeRequest -
//
void ddsTypeRequest(const char* typeName, unsigned char*& serializedTypeInternal, uint32_t& serializedTypeInternalSize)
{
  KT_T(StDds, "Got a type request callback ('%s', '%s', %d)", typeName, serializedTypeInternal, serializedTypeInternalSize);
}



// -----------------------------------------------------------------------------
//
// ddsTopicRequest -
//
void ddsTopicRequest(const char* topicName, char*& typeName, char*& serializedQos)
{
  KT_T(StDds, "Got a type request callback ('%s', '%s', '%s')", topicName, typeName, serializedQos);
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



std::unique_ptr<eprosima::ddsenabler::DDSEnabler> ddsEnabler;
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

  KT_D("Calling create_dds_enabler('%s')", configFileP);
  bool r = eprosima::ddsenabler::create_dds_enabler(configFileP,
                                                    ddsNotification,
                                                    ddsTypeNotification,
                                                    ddsTopicNotification,
                                                    ddsTypeRequest,
                                                    ddsTopicRequest,
                                                    ddsLog,
                                                    ddsEnabler);

  if (r == false)
    KT_X(1, "Unable to create the DDS Enabler");

  KT_D("DDS Enabler created");

  while (1)
  {
    sleep(1);
  }
}
