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
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjArray, ...
}

#include "dds/ddsSubscribe.h"                               // ddsSubscribe

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
void ddsNotification(KjNode* notificationP)
{
  if (ddsDumpArray == NULL)
    ddsDumpArray = kjArray(NULL, "ddsDumpArray");

  kjChildAdd(ddsDumpArray, notificationP);
}



extern __thread KjNode* uriParams;
// -----------------------------------------------------------------------------
//
// postDdsSub -
//
KjNode* postDdsSub(int* statusCodeP)
{
  KjNode*      ddsTopicTypeNodeP  = (uriParams         != NULL)? kjLookup(uriParams, "ddsTopicType") : NULL;
  const char*  ddsTopicType       = (ddsTopicTypeNodeP != NULL)? ddsTopicTypeNodeP->value.s : NULL;
  KjNode*      ddsTopicNameNodeP  = (uriParams         != NULL)? kjLookup(uriParams, "ddsTopicName") : NULL;
  const char*  ddsTopicName       = (ddsTopicNameNodeP != NULL)? ddsTopicNameNodeP->value.s : NULL;

  if (ddsTopicName == NULL || ddsTopicType == NULL)
  {
    KT_E("Both Name and Type of the topic should not be null");
    *statusCodeP = 400;
    return ftErrorResponse(400, "URI Param missing", "Both Name and Type of the topic must be present");
  }

  KT_V("Creating DDS Subcription for the topic %s:%s", ddsTopicType, ddsTopicName);
  ddsSubscribe(ddsTopicType, ddsTopicName, ddsNotification);

  *statusCodeP = 201;
  return NULL;
}
