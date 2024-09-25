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
extern "C"
{
#include "kbase/kStringSplit.h"                             // kStringSplit
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjArray, ...
#include "kjson/kjClone.h"                                  // kjClone
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // Trace levels for ktrace
#include "dds/kjTreeLog.h"                                  // kjTreeLog2

#include "ftClient/ftErrorResponse.h"                       // ftErrorResponse



// -----------------------------------------------------------------------------
//
//  ddsDumpArray - accumulating data from DDS notifications
//
extern KjNode* ddsDumpArray;



// -----------------------------------------------------------------------------
//
// ddsNotification - callback for notifications over DDS
//
// attrValue: {"attributeValue":{"type":"Property","value":2}}
//
void ddsNotification(const char* entityType, const char* entityId, const char* attrName, KjNode* attrValue)
{
  KT_T(StDdsDump, "Got a DDS notification");

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
}



extern __thread KjNode* uriParams;
// -----------------------------------------------------------------------------
//
// postDdsSub -
//
KjNode* postDdsSub(int* statusCodeP)
{
#if 0
  KjNode*      ddsTopicTypeNodeP  = (uriParams         != NULL)? kjLookup(uriParams, "ddsTopicType") : NULL;
  const char*  ddsTopicType       = (ddsTopicTypeNodeP != NULL)? ddsTopicTypeNodeP->value.s : NULL;
  KjNode*      ddsTopicNameNodeP  = (uriParams         != NULL)? kjLookup(uriParams, "ddsTopicName") : NULL;
  char*        ddsTopicName       = (ddsTopicNameNodeP != NULL)? ddsTopicNameNodeP->value.s : NULL;

  if (ddsTopicName == NULL || ddsTopicType == NULL)
  {
    KT_E("Both Name and Type of the topic can not be null");
    *statusCodeP = 400;
    return ftErrorResponse(400, "URI Param missing", "Both Name and Type of the topic must be present");
  }

  KT_V("Creating DDS Subcription for the topic %s:%s", ddsTopicType, ddsTopicName);

  char* attrV[100];
  int   attrs = kStringSplit(ddsTopicName, ',', attrV, 100);

  for (int ix = 0; ix < attrs; ix++)
    ddsSubscribe(ddsTopicType, attrV[ix], ddsNotification);

  *statusCodeP = 201;
#else
  KT_W(("Already subscribing to everything - some day this might change"));
#endif
  return NULL;
}
