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

#include "ddsenabler/dds_enabler_runner.hpp"                // dds enabler

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kbase/kStringSplit.h"                             // kStringSplit
#include "kjson/kjson.h"                                    // Kjson
#include "kjson/KjNode.h"                                   // KjNode
}

#include "orionld/common/traceLevels.h"                     // kjTreeLog2
#include "orionld/common/orionldState.h"                    // configFile
#include "orionld/kjTree/kjNavigate.h"                      // kjNavigate
#include "orionld/dds/ddsConfigTopicToAttribute.h"          // ddsConfigTopicToAttribute - for debugging only
#include "orionld/dds/ddsCategoryToKlogSeverity.h"          // ddsCategoryToKlogSeverity
#include "orionld/dds/ddsConfigLoad.h"                      // ddsConfigLoad
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsNotification.h"                    // ddsNotification
#include "orionld/dds/ddsInit.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// ddsOpMode -
//
DdsOperationMode ddsOpMode;



// -----------------------------------------------------------------------------
//
// ddsTypeNotification -
//
void ddsTypeNotification(const char* typeName, const char* topicName, const char* serializedType)
{
  KT_T(StDds, "Got a type notification ('%s', '%s', '%s')", typeName, topicName, serializedType);
}



// -----------------------------------------------------------------------------
//
// ddsLog -
//
void ddsLog(const char* fileName, int lineNo, const char* funcName, int category, const char* msg)
{
  int  level    = 0;
  char severity = ddsCategoryToKlogSeverity(category, &level);

  char* filename = (fileName != NULL)? (char*) fileName : (char*) "no-filename";
  char* funcname = (funcName != NULL)? (char*) funcName : (char*) "no-funcname";

  ktOut(filename, lineNo, funcname,  severity, level, msg);
}



// -----------------------------------------------------------------------------
//
// ddsInit - initialization function for DDS
//
// PARAMETERS
// * mode - the DDS mode the broker is working in
//
int ddsInit(Kjson* kjP, DdsOperationMode _ddsOpMode)
{
  ddsOpMode = _ddsOpMode;  // Not yet in use ... invent usage or remove !

  //
  // DDS Configuration File
  //
  errno = 0;
  if ((configFile[0] != 0) && (access(configFile, R_OK) == 0))
  {
    if (ddsConfigLoad(kjP, configFile) != 0)
      KT_X(1, "Error reading/parsing the DDS config file '%s'", configFile);

#if 0
    extern KjNode* ddsConfigTree;
    kjTreeLog2(ddsConfigTree, "DDS Config", StDdsConfig);
    KT_T(StDdsConfig, "Topics:");
    const char*  path[4] = { "dds", "ngsild", "topics", NULL };
    KjNode*      topics  = kjNavigate(ddsConfigTree, path , NULL, NULL);

    if (topics != NULL)
    {
      for (KjNode* topicP = topics->value.firstChildP; topicP != NULL; topicP = topicP->next)
      {
        char* entityId   = (char*) "N/A";
        char* entityType = (char*) "N/A";
        char* attribute = ddsConfigTopicToAttribute(topicP->name, &entityId, &entityType);

        KT_T(StDdsConfig, "Topic:         '%s':", topicP->name);
        KT_T(StDdsConfig, "  Attribute:   '%s'", attribute);
        KT_T(StDdsConfig, "  Entity ID:   '%s'", entityId);
        KT_T(StDdsConfig, "  Entity Type: '%s'", entityType);
      }
    }
#endif
  }

  KT_T(StDds, "Calling init_dds_enabler('%s')", configFile);
  eprosima::ddsenabler::init_dds_enabler(configFile, ddsNotification, ddsTypeNotification, ddsLog);

  return 0;
}
