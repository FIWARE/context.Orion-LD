/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include <stdio.h>                                               // snprintf
#include <string.h>                                              // strerror, memset
#include <unistd.h>                                              // close
#include <netdb.h>                                               // getaddrinfo, freeaddrinfo, gai_strerror
#include <errno.h>                                               // errno
#include <sys/types.h>                                           // types
#include <sys/socket.h>                                          // socket
#include <netinet/in.h>                                          // sockaddr_in

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/orionldServerConnect.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// orionldServerConnect -
//
int orionldServerConnect(const char* ip, uint16_t portNo)
{
  // getaddrinfo is POSIX-mandated thread-safe; the older gethostbyname
  // writes into a single process-wide static hostent and crashed under
  // concurrent notification delivery when two MHD threads raced on that
  // shared buffer.
  struct addrinfo   hints;
  struct addrinfo*  res = NULL;
  char              portStr[16];
  int               fd;

  KT_T(KtNotificationMsg, "Connecting to IP: '%s'", ip);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = AI_NUMERICSERV;  // port is a numeric string — skip servent lookup

  snprintf(portStr, sizeof(portStr), "%u", portNo);

  int rc = getaddrinfo(ip, portStr, &hints, &res);
  if (rc != 0 || res == NULL)
  {
    KT_E("unable to resolve host '%s': %s", ip, gai_strerror(rc));
    return -1;
  }

  fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd == -1)
  {
    KT_E("Can't even create a socket: %s", strerror(errno));
    freeaddrinfo(res);
    return -1;
  }

  if (connect(fd, res->ai_addr, res->ai_addrlen) == -1)
  {
    KT_E("Unable to connect to host/port: %s:%d", ip, portNo);
    close(fd);
    freeaddrinfo(res);
    return -1;
  }

  freeaddrinfo(res);
  return fd;
}
