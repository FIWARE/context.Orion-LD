/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include <microhttpd.h>                                          // MHD

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}


#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // Tl*
#include "orionld/mhd/mhdRequest.h"                              // mhdRequest
#include "orionld/mhd/mhdRequestEnded.h"                         // mhdRequestEnded
#include "orionld/mhd/mhdStart.h"                                // Own interface



// -----------------------------------------------------------------------------
//
// FIXME: Move to orionldState.h
//
extern unsigned int   mhdPoolSize;
extern char*          httpsKey;
extern char*          httpsCertificate;
extern unsigned int   mhdMemoryLimit;
extern unsigned int   mhdTimeout;
extern unsigned int   mhdMaxConnections;

MhdRequestInit  mhdRequestInitF  = NULL;
MhdRequestBody  mhdRequestBodyF  = NULL;
MhdRequestTreat mhdRequestTreatF = NULL;



// -----------------------------------------------------------------------------
//
// mhdStart -
//
bool mhdStart
(
  unsigned short   ldPort,
  int              ipVersion,
  MhdRequestInit   initF,
  MhdRequestBody   bodyF,
  MhdRequestTreat  treatF,
  MhdRequestEnd    endF
)
{
  mhdRequestInitF  = initF;   // Default to be mhdConnectionInit
  mhdRequestBodyF  = bodyF;   // Default to be mhdConnectionPayloadRead
  mhdRequestTreatF = treatF;  // Default to be mhdConnectionTreat

  MHD_Daemon*          mhdDaemon = NULL;
  bool                 https     = (httpsKey != NULL) && (httpsCertificate != NULL);
  bool                 ip4       = (ipVersion == 4 || ipVersion == 10);
  bool                 ip6       = (ipVersion == 6 || ipVersion == 10);
  bool                 ok        = true;
  int                  serverMode;
  struct sockaddr_in   sad4;
  struct sockaddr_in6  sad6;

  mhdMemoryLimit *= 1024;  // kilobytes turned to bytes

  //
  // Server Mode
  //
  serverMode = (mhdPoolSize > 0)? MHD_USE_SELECT_INTERNALLY | MHD_USE_EPOLL : MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD;

  // Logging - Might be the library must be compiled for debugging for this to take effect. Need to find out
  serverMode |= MHD_USE_ERROR_LOG | MHD_USE_DEBUG;


  //
  // Socket addresses
  //
  if (ip4 == true)
  {
    memset(&sad4, 0, sizeof(sad4));
    if (inet_pton(AF_INET, "0.0.0.0", &(sad4.sin_addr.s_addr)) != 1)
      KT_X(1, "Fatal Error - inet_pton failed for bind address '0.0.0.0'");
    sad4.sin_family = AF_INET;
    sad4.sin_port   = htons(ldPort);
  }

  if (ip6 == true)
  {
    memset(&sad6, 0, sizeof(sad6));
    if (inet_pton(AF_INET6, "::", &sad6.sin6_addr.s6_addr) != 1)
      KT_X(1, "Fatal Error - inet_pton failed for bind address '::'");

    sad6.sin6_family = AF_INET6;
    sad6.sin6_port = htons(ldPort);
  }

  KT_T(StMhdInit, "mhdPoolSize:       %d", mhdPoolSize);
  KT_T(StMhdInit, "mhdTimeout:        %d", mhdTimeout);
  KT_T(StMhdInit, "mhdMemoryLimit:    %d", mhdMemoryLimit);
  KT_T(StMhdInit, "mhdMaxConnections: %d", mhdMaxConnections);
  KT_T(StMhdInit, "httpsKey:          '%s'", httpsKey);
  KT_T(StMhdInit, "httpsCertificate:  '%s'", httpsCertificate);

  if (ip4 == true)
  {
    if (https == true)
    {
      serverMode |= MHD_USE_SSL;
      mhdDaemon = MHD_start_daemon(serverMode,
                                   htons(ldPort),
                                   NULL,
                                   NULL,
                                   mhdRequest,                          NULL,
                                   MHD_OPTION_NOTIFY_COMPLETED,         endF, NULL,
                                   MHD_OPTION_CONNECTION_TIMEOUT,       mhdTimeout,
                                   MHD_OPTION_SOCK_ADDR,                (struct sockaddr*) &sad4,
                                   MHD_OPTION_THREAD_POOL_SIZE,         mhdPoolSize,
                                   MHD_OPTION_CONNECTION_MEMORY_LIMIT,  mhdMemoryLimit,
                                   MHD_OPTION_CONNECTION_LIMIT,         mhdMaxConnections,
                                   MHD_OPTION_HTTPS_MEM_KEY,            httpsKey,
                                   MHD_OPTION_HTTPS_MEM_CERT,           httpsCertificate,
                                   MHD_OPTION_END);
    }
    else
      mhdDaemon = MHD_start_daemon(serverMode,
                                   htons(ldPort),
                                   NULL,
                                   NULL,
                                   mhdRequest,                          NULL,
                                   MHD_OPTION_NOTIFY_COMPLETED,         endF, NULL,
                                   MHD_OPTION_CONNECTION_TIMEOUT,       mhdTimeout,
                                   MHD_OPTION_SOCK_ADDR,                (struct sockaddr*) &sad4,
                                   MHD_OPTION_THREAD_POOL_SIZE,         mhdPoolSize,
                                   MHD_OPTION_CONNECTION_MEMORY_LIMIT,  mhdMemoryLimit,
                                   MHD_OPTION_CONNECTION_LIMIT,         mhdMaxConnections,
                                   MHD_OPTION_END);

    KT_D("mhdDaemon: %p", mhdDaemon);
    if (mhdDaemon == NULL)
      ok = false;
  }

  if (ip6 == true)
  {
    if (https == true)
    {
      serverMode |= MHD_USE_SSL;
      mhdDaemon = MHD_start_daemon(serverMode,
                                   htons(ldPort),
                                   NULL,
                                   NULL,
                                   mhdRequest,                          NULL,
                                   MHD_OPTION_NOTIFY_COMPLETED,         endF, NULL,
                                   MHD_OPTION_CONNECTION_TIMEOUT,       mhdTimeout,
                                   MHD_OPTION_SOCK_ADDR,                (struct sockaddr*) &sad6,
                                   MHD_OPTION_THREAD_POOL_SIZE,         mhdPoolSize,
                                   MHD_OPTION_CONNECTION_MEMORY_LIMIT,  mhdMemoryLimit,
                                   MHD_OPTION_CONNECTION_LIMIT,         mhdMaxConnections,
                                   MHD_OPTION_HTTPS_MEM_KEY,            httpsKey,
                                   MHD_OPTION_HTTPS_MEM_CERT,           httpsCertificate,
                                   MHD_OPTION_END);
    }
    else
      mhdDaemon = MHD_start_daemon(serverMode,
                                   htons(ldPort),
                                   NULL,
                                   NULL,
                                   mhdRequest,                          NULL,
                                   MHD_OPTION_NOTIFY_COMPLETED,         endF, NULL,
                                   MHD_OPTION_CONNECTION_TIMEOUT,       mhdTimeout,
                                   MHD_OPTION_SOCK_ADDR,                (struct sockaddr*) &sad6,
                                   MHD_OPTION_THREAD_POOL_SIZE,         mhdPoolSize,
                                   MHD_OPTION_CONNECTION_MEMORY_LIMIT,  mhdMemoryLimit,
                                   MHD_OPTION_CONNECTION_LIMIT,         mhdMaxConnections,
                                   MHD_OPTION_END);

    KT_D("mhdDaemon: %p", mhdDaemon);
    if (mhdDaemon == NULL)
      ok = false;
  }

  if (ok == false)
    KT_X(1, "Error starting the REST interface on port %d", ldPort);

  return true;
}
