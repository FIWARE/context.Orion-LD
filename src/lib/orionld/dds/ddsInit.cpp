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
#include <stdlib.h>                                         // malloc
#include <memory>                                           // for std::unique_ptr

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kbase/kStringSplit.h"                             // kStringSplit
#include "kjson/kjson.h"                                    // Kjson
#include "kjson/KjNode.h"                                   // KjNode
}

#include "logMsg/logMsg.h"                                  // lmOut

#include "orionld/types/DdsType.h"                          // DdsType
#include "orionld/common/traceLevels.h"                     // kjTreeLog2
#include "orionld/common/orionldState.h"                    // configFile
#include "orionld/kjTree/kjNavigate.h"                      // kjNavigate
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsTypes.h"                           // ddsTypeNotification, ddsTypeLookup
#include "orionld/dds/ddsNotification.h"                    // ddsNotification
#include "orionld/dds/ddsCategoryToKlogSeverity.h"          // ddsCategoryToKlogSeverity
#include "orionld/dds/ddsInit.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// ddsEnabler -
//
std::unique_ptr<eprosima::ddsenabler::DDSEnabler>  ddsEnabler;



// -----------------------------------------------------------------------------
//
// ddsTopicNotification -
//
static void ddsTopicNotification(const char* topicName, const char* typeName, const char* serializedQos)
{
  KT_T(StDds, "Got a topic notification ('%s', '%s', '%s')", topicName, typeName, serializedQos);

  DdsType* typeP = ddsTypeLookup(typeName);

  if (typeP != NULL)
    typeP->topic = strdup(topicName);

  ddsTypeList();
}



// -----------------------------------------------------------------------------
//
// ddsTypeRequest -
//
static void ddsTypeRequest
(
  const char*      typeName,
  unsigned char*&  serializedTypeInternal,
  uint32_t&        serializedTypeInternalSize
)
{
  KT_T(StDdsTypes, "Got a type request callback ('%s', '%s', %d)", typeName, serializedTypeInternal, serializedTypeInternalSize);
}



// -----------------------------------------------------------------------------
//
// ddsTopicRequest -
//
static void ddsTopicRequest(const char* topicName, char*& typeName, char*& serializedQos)
{
  KT_T(StDds, "Got a type request callback ('%s', '%s', '%s')", topicName, typeName, serializedQos);
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

#if 1
  ktOut(filename, lineNo, funcname,  severity, level, msg);
#else
  lmOut((char*) msg, severity, filename, lineNo, funcname, level);
#endif
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
  KT_T(StDds, "Calling create_dds_enabler('%s')", configFile);

  eprosima::utils::Log::ReportFilenames(true);
  bool r = eprosima::ddsenabler::create_dds_enabler(configFile,
                                                    ddsNotification,
                                                    ddsTypeNotification,
                                                    ddsTopicNotification,
                                                    ddsTypeRequest,
                                                    ddsTopicRequest,
                                                    ddsLog,
                                                    ddsEnabler);

  if (r == false)
    KT_X(1, "Unable to create the DDS Enabler");

  KT_T(StDds, "DDS Enabler created");
  return 0;
}
