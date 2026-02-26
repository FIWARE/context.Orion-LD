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
#include <stdlib.h>                                              // free
#include <string.h>                                              // strcmp

extern "C"
{
#include <microhttpd.h>                                          // MHD_upgrade_action
#include <microhttpd_ws.h>                                       // MHD_websocket_stream_free
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "cache/subCache.h"                                      // subCacheItemLookup
#include "cache/CachedSubscription.h"                            // CachedSubscription

#include "orionld/common/orionldState.h"                         // orionldState, orionldStateInit
#include "orionld/common/tenantList.h"                           // tenant0
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocSubscriptionDelete.h"             // mongocSubscriptionDelete
#include "orionld/mongoc/mongocConnectionRelease.h"              // mongocConnectionRelease
#include "orionld/ws/WsConnection.h"                             // WsConnection
#include "orionld/ws/wsConnectionList.h"                         // wsConnectionRemove
#include "orionld/ws/wsClose.h"                                  // Own interface





// -----------------------------------------------------------------------------
//
// wsClose - close a WS connection, DELETE the associated subscription, clean up
//
// When a WS connection closes:
// 1. Find the associated CachedSubscription
// 2. Delete it from MongoDB
// 3. Remove it from the sub-cache
// 4. Free the WS stream and close the socket via MHD
// 5. Remove from the global WS connection list
// 6. Free the WsConnection struct
//
void wsClose(WsConnection* wsP)
{
  KT_T(KtWsTest, "WS connection closed (fd=%d, subId=%s)",
       (int) wsP->fd,
       wsP->subscriptionId ? wsP->subscriptionId : "none");
  KT_T(StWs, "Closing WS connection (fd=%d, subId=%s)",
       (int) wsP->fd,
       wsP->subscriptionId ? wsP->subscriptionId : "none");

  //
  // DELETE the associated subscription (if any)
  //
  if (wsP->subscriptionId != NULL)
  {
    CachedSubscription* cSubP = subCacheItemLookup(wsP->tenantName, wsP->subscriptionId);

    if (cSubP != NULL)
    {
      //
      // Delete from MongoDB
      // Don't call orionldStateInit() - MHD will call requestCompleted later
      // and we must not overwrite the thread's orionldState.
      // Just set tenantP for mongocConnectionGet (called inside mongocSubscriptionDelete).
      //
      orionldState.tenantP = &tenant0;  // TODO: look up tenant by wsP->tenantName

      if (mongocSubscriptionDelete(wsP->subscriptionId) == false)
        KT_W("Failed to delete subscription '%s' from DB on WS close", wsP->subscriptionId);
      else
        KT_T(StWs, "Deleted subscription '%s' from DB on WS close", wsP->subscriptionId);

      // Remove from sub-cache
      subCacheItemRemove(cSubP);

      // Release mongoc connection back to the pool
      if (orionldState.mongoc.subscriptionsP != NULL)
      {
        mongoc_collection_destroy(orionldState.mongoc.subscriptionsP);
        orionldState.mongoc.subscriptionsP = NULL;
      }
      mongocConnectionRelease();
    }
    else
    {
      KT_W("WS close: subscription '%s' not found in cache", wsP->subscriptionId);
    }
  }

  // Free the WebSocket stream
  if (wsP->ws != NULL)
  {
    MHD_websocket_stream_free(wsP->ws);
    wsP->ws = NULL;
  }

  // Close the socket via MHD
  if (wsP->urh != NULL)
  {
    MHD_upgrade_action(wsP->urh, MHD_UPGRADE_ACTION_CLOSE);
    wsP->urh = NULL;
  }

  // Remove from the connection list
  wsConnectionRemove(wsP);

  // Free allocated strings
  free(wsP->subscriptionId);
  free(wsP->tenantName);

  // Free the connection struct itself
  free(wsP);
}
