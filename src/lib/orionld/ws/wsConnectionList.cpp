/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include <string.h>                                              // strcmp
#include <pthread.h>                                             // pthread_mutex_t

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsConnectionList.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// Global linked list of WS connections, protected by a mutex
//
static WsConnection*    wsConnectionList     = NULL;
static pthread_mutex_t  wsConnectionListMutex = PTHREAD_MUTEX_INITIALIZER;



// -----------------------------------------------------------------------------
//
// wsConnectionAdd -
//
void wsConnectionAdd(WsConnection* wsP)
{
  pthread_mutex_lock(&wsConnectionListMutex);
  wsP->next          = wsConnectionList;
  wsConnectionList   = wsP;
  pthread_mutex_unlock(&wsConnectionListMutex);

  KT_T(StWs, "WS connection added (fd=%d)", (int) wsP->fd);
}



// -----------------------------------------------------------------------------
//
// wsConnectionRemove -
//
void wsConnectionRemove(WsConnection* wsP)
{
  pthread_mutex_lock(&wsConnectionListMutex);

  if (wsConnectionList == wsP)
  {
    wsConnectionList = wsP->next;
  }
  else
  {
    WsConnection* prev = wsConnectionList;
    while (prev != NULL && prev->next != wsP)
      prev = prev->next;

    if (prev != NULL)
      prev->next = wsP->next;
  }

  pthread_mutex_unlock(&wsConnectionListMutex);

  KT_T(StWs, "WS connection removed (fd=%d, subId=%s)", (int) wsP->fd, wsP->subscriptionId ? wsP->subscriptionId : "none");
}



// -----------------------------------------------------------------------------
//
// wsConnectionLookup - find a WS connection by subscription ID
//
WsConnection* wsConnectionLookup(const char* subscriptionId)
{
  WsConnection* wsP = NULL;

  pthread_mutex_lock(&wsConnectionListMutex);

  WsConnection* p = wsConnectionList;
  while (p != NULL)
  {
    if ((p->subscriptionId != NULL) && (strcmp(p->subscriptionId, subscriptionId) == 0))
    {
      wsP = p;
      break;
    }
    p = p->next;
  }

  pthread_mutex_unlock(&wsConnectionListMutex);

  return wsP;
}
