/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjObject
#include "kjson/kjNavigate.h"                                    // kjNavigate
}

#include "logMsg/logMsg.h"                                       // LM_*


#include "orionld/types/OrionLdRestService.h"                    // OrionLdRestService
#include "orionld/common/orionldState.h"                         // orionldState, ddsSupport
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/config/configInit.h"                           // configTree



// ----------------------------------------------------------------------------
//
// orionldGetDdsTopics -
//
bool orionldGetDdsTopics(void)
{
  orionldState.noLinkHeader   = true;  // We don't want the Link header for this service

  if (ddsSupport == false)
  {
    orionldError(OrionldOperationNotSupported, "DDS is not enabled", orionldState.serviceP->url, 501);
    return false;
  }

  if (configTree == NULL)
  {
    orionldError(OrionldResourceNotFound, "No Topics Found", "No config file found", 404);
    return false;
  }

  const char*    path[4] = { "dds", "ngsild", "topics", NULL };
  static KjNode* topicsP = kjNavigate(configTree, path, NULL, NULL);

  if (topicsP == NULL)
  {
    orionldError(OrionldResourceNotFound, "No Topics Found", "The config item 'dds.ngsild.topics' doesn't exist", 404);
    return false;
  }

  orionldState.httpStatusCode = 200;
  orionldState.responseTree   = topicsP;

  return true;
}
