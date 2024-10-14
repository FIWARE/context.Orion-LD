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

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "ktrace/ktGlobals.h"                               // globals for KT library
#include "kargs/kargs.h"                                    // argument parsing - kargs library
#include "kalloc/kaInit.h"                                  // kaInit
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjBuilder.h"                                // kjObject, ...
}

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
char*          traceLevels;
char*          logDir        = NULL;
char*          logLevel;
KBool          logToScreen;
KBool          fixme;
unsigned short ldPort;
char*          httpsKey;
char*          httpsCertificate;
unsigned int   mhdPoolSize;
unsigned int   mhdMemoryLimit;
unsigned int   mhdTimeout;
unsigned int   mhdMaxConnections;
bool           distributed;
long long      inReqPayloadMaxSize  = 64 * 1024;
char*          configFile = NULL;



// -----------------------------------------------------------------------------
//
// kargs - vector of CLI parameters
//
KArg kargs[] =
{
  //
  // Potential builtins
  //
  { "--trace",            "-t",     KaString,  &traceLevels,          KaOpt, 0,         KA_NL,    KA_NL,    "trace levels (csv of levels/ranges)"               },
  { "--logDir",           "-ld",    KaString,  &logDir,               KaOpt, _i "/tmp", KA_NL,    KA_NL,    "log file directory"                                },
  { "--logLevel",         "-ll",    KaString,  &logLevel,             KaOpt, 0,         KA_NL,    KA_NL,    "log level (ERR|WARN|INFO|INFO|VERBOSE|TRACE|DEBUG" },
  { "--logToScreen",      "-ls",    KaBool,    &logToScreen,          KaOpt, KFALSE,    KA_NL,    KA_NL,    "log to screen"                                     },
  { "--fixme",            "-fix",   KaBool,    &fixme,                KaOpt, KFALSE,    KA_NL,    KA_NL,    "FIXME messages"                                    },
  { "--config",           "-cfg",   KaString,  &configFile,           KaOpt, NULL,      KA_NL,    KA_NL,    "Config File"                                       },

  //
  // Broker options
  //
  { "--port",             "-p",     KaUShort,  &ldPort,               KaOpt, _i 7701,   _i 1027,  _i 65535, "TCP port for incoming requests"                    },
  { "--httpsKey",         "-k",     KaString,  &httpsKey,             KaOpt, NULL,      KA_NL,    KA_NL,    "https key file"                                    },
  { "--httpsCertificate", "-c",     KaString,  &httpsCertificate,     KaOpt, NULL,      KA_NL,    KA_NL,    "https certificate file"                            },
  { "--distOps",          "-dops",  KaBool,    &distributed,          KaOpt, KFALSE,    KA_NL,    KA_NL,    "support for distributed operations"                },

  // MHD
  { "--mhdPoolSize",      "-mps",   KaUInt,    &mhdPoolSize,          KaOpt, _i 8,      _i 0,     _i 1024,  "MHD request thread pool size"                      },
  { "--mhdMemoryLimit",   "-mlim",  KaUInt,    &mhdMemoryLimit,       KaOpt, _i 64,     _i 0,     _i 1024,  "MHD memory limit (in kb)"                          },
  { "--mhdTimeout",       "-mtmo",  KaUInt,    &mhdTimeout,           KaOpt, _i 2000,   _i 0,     KA_NL,    "MHD connection timeout (in milliseconds)"          },
  { "--mhdConnections",   "-mcon",  KaUInt,    &mhdMaxConnections,    KaOpt, _i 512,    _i 1,     KA_NL,    "Max number of MHD connections"                     },

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
static void ddsNotification(const char* typeName, const char* topicName, const char* json, int64_t publishTime)
{
  KT_T(StDdsDump, "Got a notification on %s:%s (json: %s)", typeName, topicName, json);

#if 0
  KT_T(StDdsDump, "Need to check the publishTime (%lld) to perhaps discard", publishTime);

  if (ddsDumpArray == NULL)
  {
    KT_T(StDdsDump, "Creating the DDS DumpArray");
    ddsDumpArray = kjArray(NULL, "ddsDumpArray");
  }

  KjNode* entityTypeNode = kjString(NULL, "entityType", entityType);
  KjNode* entityIdNode   = kjString(NULL, "entityId",   entityId);
  KjNode* notificationP  = kjObject(NULL, NULL);

  //
  // The value of the attribute (right now) comes as { "attributeValue": xxx }
  // Assuming DDS knows only about Property, we change the name "attributeValue" to "value"
  //
  if ((attrValue->type == KjObject) && (attrValue->value.firstChildP != NULL))
  {
    kjTreeLog2(attrValue, attrName, StDdsDump);
    attrValue = attrValue->value.firstChildP;
  }

  attrValue->name = (char*) attrName;

  kjChildAdd(notificationP, entityTypeNode);
  kjChildAdd(notificationP, entityIdNode);
  kjChildAdd(notificationP, kjClone(NULL, attrValue));

  kjTreeLog2(ddsDumpArray, "DDS dump array before", StDdsDump);
  kjTreeLog2(notificationP, "Adding to DDS dump array", StDdsDump);

  kjChildAdd(ddsDumpArray, notificationP);
  kjTreeLog2(ddsDumpArray, "DDS dump array after", StDdsDump);
#endif
}



// -----------------------------------------------------------------------------
//
// ddsTypeNotification -
//
static void ddsTypeNotification(const char* typeName, const char* topicName, const char* serializedType)
{
  KT_T(StDds, "Got a type notification ('%s', '%s', '%s')", typeName, topicName, serializedType);
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
  if (configFile == NULL)
  {
    char* home = getenv("HOME");
    if (home != NULL)
    {
      snprintf(configFilePath, sizeof(configFilePath) - 1, "%s/.ftClient", home);
      configFile = configFilePath;
    }
  }

  if (configFile != NULL)
    configFile = strdup(configFile);

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

  if (eprosima::ddsenabler::init_dds_enabler(configFile, ddsNotification, ddsTypeNotification, ddsLog) != 0)
    KT_X(1, "Unable to initialize the DDS Enabler");

  while (1)
  {
    sleep(1);
  }
}
