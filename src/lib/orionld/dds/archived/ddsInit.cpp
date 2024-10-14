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

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kbase/kStringSplit.h"                             // kStringSplit
#include "kjson/kjson.h"                                    // Kjson
#include "kjson/KjNode.h"                                   // KjNode
}

#include "orionld/common/traceLevels.h"                     // kjTreeLog2
#include "orionld/kjTree/kjNavigate.h"                      // kjNavigate
#include "orionld/dds/ddsSubscribe.h"                       // ddsSubscribe
#include "orionld/dds/ddsNotification.h"                    // ddsNotification
#include "orionld/dds/ddsConfigLoad.h"                      // ddsConfigLoad
#include "orionld/dds/ddsConfigTopicToAttribute.h"          // ddsConfigTopicToAttribute  - TMP: debugging
#include "orionld/dds/kjTreeLog.h"                          // kjTreeLog2
#include "orionld/dds/ddsInit.h"                            // Own interface



// -----------------------------------------------------------------------------
//
// ddsOpMode -
//
DdsOperationMode ddsOpMode;



// -----------------------------------------------------------------------------
//
// ddsInit - initialization function for DDS
//
// PARAMETERS
// * ddsTopicType
// * ddsSubsTopics
// * mode - the DDS mode the broker is working in
//
int ddsInit(Kjson* kjP, const char* ddsConfigFile, const char* ddsTopicType, char* ddsSubsTopics, DdsOperationMode _ddsOpMode)
{
  ddsOpMode = _ddsOpMode;  // Not yet in use ... invent usage or remove !

  //
  // DDS Configuration File
  //
  errno = 0;
  if (access(ddsConfigFile, R_OK) == 0)
  {
    if (ddsConfigLoad(kjP, ddsConfigFile) != 0)
      KT_X(1, "Error reading/parsing the DDS config file '%s'", ddsConfigFile);

#ifdef DEBUG
    extern KjNode* ddsConfigTree;
    kjTreeLog2(ddsConfigTree, "DDS Config", StDdsConfig);
    KT_T(StDdsConfig, "Topics:");
    const char*  path[3] = { "dds", "topics", NULL };
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
//  else
//    KT_X(1, ("Unable to read the DDS config file '%s' (%s)", ddsConfigFile, strerror(errno));


  //
  // DDS Subscriptions
  //
  // For now, the topics to subscribe to is input to the broker, as a CLI parameter with
  // the topics as a comma-separated list.
  // This is temporary, just to be able to test things.
  //
  // I imagine in the end, all DDS topics will be found via dicovery and the broker will
  // subscribe to all of them.  Or, perhaps some filter. We'll see.
  // For now, just a CSV.
  //
  if (ddsSubsTopics[0] == 0)
    return 0;

  KT_V("topics: %s", ddsSubsTopics);
  char* topicV[100];
  int   topics = kStringSplit(ddsSubsTopics, ',', topicV, 3);

  KT_V("no of topics: %d", topics);
  for (int ix = 0; ix < topics; ix++)
  {
    KT_V("Subscribing to DDS topic %s::%s", ddsTopicType, topicV[ix]);
    ddsSubscribe(ddsTopicType, topicV[ix], ddsNotification);
  }

  return 0;
}
