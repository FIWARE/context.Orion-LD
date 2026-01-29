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
#include <string.h>                                              // strcmp, strlen
#include <microhttpd.h>                                          // MHD

extern "C"
{
#include "kprom/kprom.h"                                         // kpromRender
}

#include "orionld/prometheus/promServer.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// Globals
//
static MHD_Daemon*  promDaemon = NULL;
static char         metricsBuffer[64 * 1024];  // 64KB buffer for metrics



// -----------------------------------------------------------------------------
//
// promRequestCallback - MHD callback for /metrics requests
//
static MHD_Result promRequestCallback
(
  void*                  cls,
  struct MHD_Connection* connection,
  const char*            url,
  const char*            method,
  const char*            version,
  const char*            uploadData,
  size_t*                uploadDataSize,
  void**                 conCls
)
{
  // Only handle GET /metrics
  if (strcmp(method, "GET") != 0 || strcmp(url, "/metrics") != 0)
  {
    const char* notFound = "Not Found\n";
    struct MHD_Response* response = MHD_create_response_from_buffer(strlen(notFound),
                                                                     (void*) notFound,
                                                                     MHD_RESPMEM_PERSISTENT);
    MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
  }

  // Render metrics
  int len = kpromRender(metricsBuffer, sizeof(metricsBuffer));

  struct MHD_Response* response = MHD_create_response_from_buffer(len,
                                                                   metricsBuffer,
                                                                   MHD_RESPMEM_MUST_COPY);
  MHD_add_response_header(response, "Content-Type", "text/plain; version=0.0.4; charset=utf-8");
  MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);

  return ret;
}



// -----------------------------------------------------------------------------
//
// promServerStart - start the Prometheus metrics server on the given port
//
int promServerStart(unsigned short port)
{
  promDaemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY,
                                 port,
                                 NULL, NULL,
                                 promRequestCallback, NULL,
                                 MHD_OPTION_END);

  return (promDaemon != NULL) ? 0 : -1;
}



// -----------------------------------------------------------------------------
//
// promServerStop - stop the Prometheus metrics server
//
void promServerStop(void)
{
  if (promDaemon != NULL)
  {
    MHD_stop_daemon(promDaemon);
    promDaemon = NULL;
  }
}
