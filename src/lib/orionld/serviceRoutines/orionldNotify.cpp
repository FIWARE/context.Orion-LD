/*
*
* Copyright 2019 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Ken Zangelin
*/
#include <string.h>                                              // strlen
#include <sys/uio.h>                                             // writev
#include <sys/select.h>                                          // select

extern "C"
{
#include "kjson/kjRender.h"                                      // kjRender
#include "kjson/kjBuilder.h"                                     // kjObject, kjArray, kjString, kjChildAdd, ...
#include "kalloc/kaAlloc.h"                                      // kaAlloc
}

#include "logMsg/logMsg.h"
#include "logMsg/traceLevels.h"

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/numberToDate.h"                         // numberToDate
#include "orionld/common/uuidGenerate.h"                         // uuidGenerate
#include "orionld/common/orionldServerConnect.h"                 // orionldServerConnect
#include "orionld/context/orionldCoreContext.h"                  // ORIONLD_CORE_CONTEXT_URL
#include "orionld/serviceRoutines/orionldNotify.h"               // Own interface



// -----------------------------------------------------------------------------
//
// ipPortAndRest - extract ip, port and URL-PATH from a 'reference' string
//
// FIXME
//   This function is generic and should be moved to its own module in orionld/common
//   However, I think I have a function doing exactly this already ...
//
static void ipPortAndRest(char* ipport, char** ipP, unsigned short* portP, char** restP)
{
  char*            colon;
  char*            ip;
  unsigned short   portNo  = 80;  // What should be the default port?
  char*            rest;

  LM_TMP(("NFY: ipport == '%s'", ipport));
  //
  // Starts with http:// ...
  //
  ip = strchr(ipport, '/');
  ip += 2;
  rest = ip;
  LM_TMP(("NFY: Host: %s", ip));

  colon = strchr(ip, ':');
  if (colon != NULL)
  {
    *colon = 0;
    portNo = atoi(&colon[1]);
    rest = &colon[1];
  }

  LM_TMP(("NFY: Host: %s", ip));
  LM_TMP(("NFY: Port: %d", portNo));
  LM_TMP(("NFY: Rest: %s (not ready)", rest));

  *ipP   = ip;
  *portP = portNo;

  rest = strchr(rest, '/');
  *restP = rest;
  LM_TMP(("NFY: Rest: %s (ready)", rest));
}



// -----------------------------------------------------------------------------
//
// responseTreat -
//
static void responseTreat(OrionldNotificationInfo* niP, char* buf, int bufLen)
{
  LM_TMP(("NFY: Reading from endpoint of subscription '%s'", niP->subscriptionId));

  int    nb             = read(niP->fd, buf, bufLen);
  char*  firstLine      = NULL;
  char*  endOfFirstLine = NULL;

  if (nb == -1)
  {
    LM_E(("Internal Error (error reading from notification endpoint: %s)", strerror(errno)));
    return;
  }

  //
  //
  //
  firstLine      = buf;
  endOfFirstLine = strchr(firstLine, '\n');

  if (endOfFirstLine == NULL)
  {
    LM_E(("Internal Error (unable to find end of first line from notification endpoint: %s)", strerror(errno)));
    return;
  }

  *endOfFirstLine = 0;

  //
  // FIXME: Read the rest of the message, using select
  //
  LM_TMP(("NFY: First line of notification response: %s", firstLine));
  niP->allOK = true;
}



// -----------------------------------------------------------------------------
//
// orionldNotify -
//
// This function assumes that the vector orionldState.notificationInfo is
// correctly filled in.
//
// orionldState.notificationInfo[x].attrsForNotification is a KjNode tree
// with all attributes for the notification, and also, the entity ID.
//
// All attribute names and the entity type are assumed to be already aliased according to the context
//
void orionldNotify(void)
{
  //
  // Preparing the HTTP headers which will be pretty much the same for all notifications
  // What differs is Content-Length, Content-Type, and the Request header
  //
  char  requestHeader[128];
  char  contentLenHeader[32];
  char* lenP                    = &contentLenHeader[16];
  char* contentTypeHeaderJson   = (char*) "Content-Type: application/json\r\n";
  char* contentTypeHeaderJsonLd = (char*) "Content-Type: application/ld+json\r\n";
  char* userAgentHeader         = (char*) "User-Agent: orionld\r\n\r\n";  // Double newline - must be the last HTTP header
  int   payloadLen              = 10000;
  char* payload                 = (char*) malloc(payloadLen + 1);

  if (payload == NULL)
    LM_X(1, ("Unable to allocate room for notification!"));

  strcpy(contentLenHeader, "Content-Length: 0");  // Can't modify inside static strings, so need a char-vec on the stack for contentLenHeader

  //
  // struct iovec
  // {
  //   void  *iov_base;    /* Starting address */
  //   size_t iov_len;     /* Number of bytes to transfer */
  // };
  //
  int           contentLength;
  struct iovec  ioVec[6]        = { { requestHeader, 0 }, { contentLenHeader, 0 }, { contentTypeHeaderJson, 32 }, { userAgentHeader, 23 }, { payload, 0 } };
  int           ioVecLen        = 5;
  int           now             = time(NULL);
  char          nowString[64];
  char*         detail;

  if (numberToDate(now, nowString, sizeof(nowString), &detail) == false)
  {
    LM_E(("Internal Error (converting timestamp to DateTime string: %s)", detail));
    snprintf(nowString, sizeof(nowString), "1970-01-01T00:00:00Z");
  }

  LM_TMP(("NFY: Sending %d notifications", orionldState.notificationRecords));

  for (int ix = 0; ix < orionldState.notificationRecords; ix++)
  {
    OrionldNotificationInfo*  niP = &orionldState.notificationInfo[ix];
    char*                     ip;
    unsigned short            port;
    char*                     rest;
    KjNode*                   notificationTree;
    char                      notificationId[64];

    notificationTree = kjObject(orionldState.kjsonP, NULL);

    strncpy(notificationId, "urn:ngsi-ld:Notification:", sizeof(notificationId));
    uuidGenerate(&notificationId[25]);

    ipPortAndRest(niP->reference, &ip, &port, &rest);
    snprintf(requestHeader, sizeof(requestHeader), "POST %s HTTP/1.1\r\n", rest);

    if (niP->mimeType == JSONLD)
    {
      //
      // For better tput, I could maintain not one ioVec but TWO.
      // One for "application/json" and another one for "application/ld+json"
      //

      //
      // Overwrite contentTypeHeaderJson in ioVec[2]
      //
      ioVec[2].iov_base = contentTypeHeaderJsonLd;
      ioVec[2].iov_len  = 35;

      // Add @context to payload
      if (orionldState.contextP == NULL)
      {
        // Core Context
        KjNode* contextNodeP = kjString(orionldState.kjsonP, "@context", ORIONLD_CORE_CONTEXT_URL);
        kjChildAdd(notificationTree, contextNodeP);
      }
      else
        kjChildAdd(notificationTree, orionldState.contextP->tree);
    }
    else
    {
      //
      // Add Link HTTP header
      //
      ioVecLen = 6;

      // Move down PAYLOAD
      ioVec[5].iov_base = ioVec[4].iov_base;
      ioVec[5].iov_len  = ioVec[4].iov_len;

      // Move down userAgentHeader - must be the last as it contains the double \r\n
      ioVec[4].iov_base = ioVec[3].iov_base;
      ioVec[4].iov_len  = ioVec[3].iov_len;

      // Now ioVec[3] is free for the Link header
      ioVec[3].iov_base = orionldState.contextP->url;
      ioVec[3].iov_len  = strlen(orionldState.contextP->url);
    }

    //
    // Fix payload
    //
    // The entity id/type + attribute list go into an object inside a vector called data.
    // In the case of POST /entities/*/attrs, as there is only ONE entity, there will be only ONE item in the data vector
    //
    // Apart from that we have the following fields:
    // * @context         (if JSONLD - already added)
    // * id               (of the Notification)
    // * type             (== "Notification")
    // * subscriptionId   (id of the subscription that provoked the Notification)
    // * notifiedAt       (DateTime of RIGHT NOW)
    //
    KjNode* idNodeP              = kjString(orionldState.kjsonP, "id", notificationId);
    KjNode* typeNodeP            = kjString(orionldState.kjsonP, "type", "Notification");
    KjNode* subscriptionIdNodeP  = kjString(orionldState.kjsonP, "subscriptionId", niP->subscriptionId);
    KjNode* notifiedAtNodeP      = kjString(orionldState.kjsonP, "notifiedAt", nowString);
    KjNode* dataNodeP            = kjArray(orionldState.kjsonP,  "data");

    kjChildAdd(notificationTree, idNodeP);
    kjChildAdd(notificationTree, typeNodeP);
    kjChildAdd(notificationTree, subscriptionIdNodeP);
    kjChildAdd(notificationTree, notifiedAtNodeP);
    kjChildAdd(notificationTree, dataNodeP);

    kjChildAdd(dataNodeP, niP->attrsForNotification);

    // LM_TMP(("NFY: Notification %d - calling kjRender to render payload body", ix));
    kjRender(orionldState.kjsonP, notificationTree, payload, payloadLen);
    // payload = (char*) "No Payload!";
    // LM_TMP(("NFY: Notification %d - after rendering payload body", ix));
    // LM_TMP(("NFY: payload: %s", payload));

    int sizeLeftForLen = 16;  // sizeof(contentLenHeader) - 16
    contentLength = strlen(payload);
    snprintf(lenP, sizeLeftForLen, "%d\r\n", contentLength);  // Writing Content-Length inside contentLenHeader

    ioVec[0].iov_len = strlen(requestHeader);
    ioVec[1].iov_len = strlen(contentLenHeader);
    ioVec[4].iov_len = contentLength;

    //
    // Data ready to send
    //
    niP->fd = orionldServerConnect(ip, port);

    if (niP->fd == -1)
    {
      niP->connected = false;
      LM_E(("Internal Error (unable to connent to server for notification for subscription '%s': %s)", niP->subscriptionId, strerror(errno)));
      continue;
    }

    niP->connected = true;

    LM_TMP(("NFY: Sending notification for subscription '%s'", niP->subscriptionId));
    if (writev(niP->fd, ioVec, ioVecLen) == -1)
    {
      close(niP->fd);

      niP->fd        = -1;
      niP->connected = false;

      LM_E(("Internal Error (unable to send to server for notification for subscription '%s'): %s", niP->subscriptionId, strerror(errno)));
      continue;
    }
  }

  //
  // Receive responses
  //
  int             fds;
  fd_set          rFds;
  int             fdMax      = 0;
  int             startTime  = time(NULL);
  struct timeval  tv         = { 1, 0 };

  while (1)
  {
    //
    // Set File Descriptors for the select
    //
    FD_ZERO(&rFds);
    for (int ix = 0; ix < orionldState.notificationRecords; ix++)
    {
      OrionldNotificationInfo*  niP = &orionldState.notificationInfo[ix];

      if ((niP->fd != -1) && (niP->connected == true))
      {
        FD_SET(niP->fd, &rFds);
        fdMax = MAX(fdMax, niP->fd);
      }

      niP->allOK = false;  // Set to 'true' once the response of the notification is received
    }

    fds = select(1, &rFds, NULL, NULL, &tv);
    if ((fds == -1) && (errno != EINTR))
      LM_X(1, ("select error: %s\n", strerror(errno)));
    else if (fds > 0)
    {
      for (int ix = 0; ix < orionldState.notificationRecords; ix++)
      {
        OrionldNotificationInfo*  niP = &orionldState.notificationInfo[ix];

        if (FD_ISSET(niP->fd, &rFds))
          responseTreat(niP, payload, payloadLen);  // We reuse the allocated buffer 'payload'
      }
    }

    //
    // Timeout after 10 seconds
    //
    // FIXME: calling time() might be avoided by inspecting 'tv'
    //
    if (time(NULL) > startTime + 10)
      break;
  }

  free(payload);

  //
  // Close file descriptors and set lastFailure/lastSuccess
  //
  for (int ix = 0; ix < orionldState.notificationRecords; ix++)
  {
    OrionldNotificationInfo*  niP = &orionldState.notificationInfo[ix];

    if (niP->fd != -1)
      close(niP->fd);

#if 0
    if (niP->allOK == true)
      dbSubscriptionLastSuccessSet(niP->subscriptionId);
    else
      dbSubscriptionLastFailureSet(niP->subscriptionId);
#endif
  }
}
