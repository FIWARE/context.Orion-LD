/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include <vector>

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjArray
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "common/string.h"                                     // toString
#include "rest/uriParamNames.h"                                // URI_PARAM_PAGINATION_OFFSET, URI_PARAM_PAGINATION_LIMIT
#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "mongoBackend/mongoGetSubscriptions.h"                // mongoListSubscriptions
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/kjTree/kjTreeFromSubscription.h"             // kjTreeFromSubscription
#include "orionld/serviceRoutines/orionldGetSubscriptions.h"   // Own Interface



// ----------------------------------------------------------------------------
//
// orionldGetSubscriptions -
//
bool orionldGetSubscriptions(ConnectionInfo* ciP)
{
  std::vector<ngsiv2::Subscription> subVec;
  OrionError                        oe;
  int64_t                           count  = 0;

  LM_T(LmtServiceRoutine, ("In orionldGetSubscription"));

  mongoGetLdSubscriptions(ciP, &subVec, orionldState.tenant, (long long*) &count, &oe);

  if (orionldState.uriParams.count == true)
  {
    ciP->httpHeader.push_back("NGSILD-Results-Count");
    ciP->httpHeaderValue.push_back(toString(count));
  }

  orionldState.responseTree = kjArray(orionldState.kjsonP, NULL);

  for (unsigned int ix = 0; ix < subVec.size(); ix++)
  {
    KjNode* subscriptionNodeP = kjTreeFromSubscription(&subVec[ix]);

    kjChildAdd(orionldState.responseTree, subscriptionNodeP);
  }

  return true;
}
