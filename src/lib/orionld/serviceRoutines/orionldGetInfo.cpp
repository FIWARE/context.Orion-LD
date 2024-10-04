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
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, kjBoolean, ...
}

#include "logMsg/logMsg.h"                                     // LM_*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/numberToDate.h"                       // numberToDate
#include "orionld/serviceRoutines/orionldGetVersion.h"         // orionldGetVersion
#include "orionld/serviceRoutines/orionldGetInfo.h"            // Own Interface



// ----------------------------------------------------------------------------
//
// uptimeRender -
//
static void uptimeRender(int uptime, char* buf, int bufLen)
{
  int seconds = uptime % 60;
  uptime = uptime / 60;

  int minutes = uptime % 60;
  uptime = uptime / 60;

  int hours  = uptime / 24;
  uptime = uptime / 24;

  int days = uptime;

  if (days > 0)
    snprintf(buf, bufLen - 1, "P%dDT%02dH%02dM%02dS", days, hours, minutes, seconds);
  else
    snprintf(buf, bufLen - 1, "PT%02dH%02dM%02dS", hours, minutes, seconds);
}



// ----------------------------------------------------------------------------
//
// orionldGetInfo -
//
bool orionldGetInfo(void)
{
  static int  runNo = 1;
  char        id[128];
  int         uptime = orionldState.requestTime - startTime;
  char        uptimeV[64];
  char        currentTimeV[64];
  KjNode*     extras;

  // id
  snprintf(id, sizeof(id) - 1, "urn:ngsi-ld:cs:%06d", runNo);
  ++runNo;

  // uptime
  uptimeRender(uptime, uptimeV, sizeof(uptimeV));

  // current time
  numberToDate(orionldState.requestTime, currentTimeV, sizeof(currentTimeV));

  // extras
  orionldGetVersion();
  extras = orionldState.responseTree;

  KjNode* infoP    = kjObject(orionldState.kjsonP, NULL);
  KjNode* idP      = kjString(orionldState.kjsonP, "id", id);
  KjNode* typeP    = kjString(orionldState.kjsonP, "type", "ContextSourceIdentity");
  KjNode* uptimeP  = kjString(orionldState.kjsonP, "contextSourceUptime", uptimeV);
  KjNode* currentP = kjString(orionldState.kjsonP, "contextSourceTimeAt", currentTimeV);
  KjNode* aliasP   = kjString(orionldState.kjsonP, "contextSourceAlias", brokerId);

  kjChildAdd(infoP, idP);
  kjChildAdd(infoP, typeP);
  kjChildAdd(infoP, uptimeP);
  kjChildAdd(infoP, currentP);
  kjChildAdd(infoP, aliasP);
  kjChildAdd(infoP, extras);

  extras->name = (char*) "contextSourceExtras";

  orionldState.responseTree = infoP;

  // This request is ALWAYS returned with pretty-print
  orionldState.uriParams.prettyPrint     = true;
  orionldState.kjsonP->spacesPerIndent   = 2;
  orionldState.kjsonP->nlString          = (char*) "\n";
  orionldState.kjsonP->stringBeforeColon = (char*) "";
  orionldState.kjsonP->stringAfterColon  = (char*) " ";

  orionldState.noLinkHeader              = true;  // We don't want the Link header for version requests

  return true;
}
