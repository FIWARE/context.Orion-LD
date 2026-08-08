/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include <curl/curl.h>                                           // CURL

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "ktrace/ktTraceLevelCheck.h"                            // ktTraceLevelCheck
#include "kbase/kTime.h"                                         // kTimeGet
#include "kbase/kMacros.h"                                       // K_MAX
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildAdd
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjRender.h"                                      // kjFastRender
}

#include "orionld/types/SubCacheItem.h"                           // SubCacheItem

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/orionldPatchApply.h"                    // orionldPatchApply
#include "orionld/common/datasetInstancesMerge.h"                // datasetInstancesMerge
#include "orionld/types/OrionldAlteration.h"                     // OrionldAlteration, orionldAlterationType
#include "orionld/dbModel/dbModelToApiEntity.h"                  // dbModelToApiEntity
#include "orionld/notifications/subCacheAlterationMatch.h"       // subCacheAlterationMatch
#include "orionld/notifications/notificationSend.h"              // notificationSend
#include "orionld/notifications/notificationSuccess.h"           // notificationSuccess
#include "orionld/notifications/notificationFailure.h"           // notificationFailure
#include "orionld/notifications/orionldAlterationsTreat.h"       // Own interface



// -----------------------------------------------------------------------------
//
// NotificationPending -
//
typedef struct NotificationPending
{
  SubCacheItem*                subP;
  int                          fd;
  CURL*                        curlHandleP;
  bool                         used;
  struct NotificationPending*  next;
} NotificationPending;



// -----------------------------------------------------------------------------
//
// statusCodeExtract -
//
static int statusCodeExtract(char* startLine)
{
  char* space = strchr(startLine, ' ');

  if (space == NULL)
    return -1;

  return atoi(&space[1]);
}



// -----------------------------------------------------------------------------
//
// readWithTimeout - FIXME: to its own module under orionld/common
//
int readWithTimeout(int fd, char* buf, int bufLen, int tmoSecs, int tmoMicroSecs)
{
  while (1)  // "try-again" if EINTR, otherwise, either return error or finish
  {
    int            fds;
    fd_set         rFds;
    struct timeval tv    = { tmoSecs, tmoMicroSecs };
    int            fdMax = fd;

    FD_ZERO(&rFds);
    FD_SET(fd, &rFds);

    fds = select(fdMax + 1, &rFds, NULL, NULL, &tv);
    if (fds == -1)
    {
      if (errno == EINTR)
        continue;

      KT_E("select error: %s", strerror(errno));
      return -1;
    }
    else if (fds == 0)
      return 0;

    if (!FD_ISSET(fd, &rFds))
      return -1;  // This can't happen ...

    break;
  }

  return read(fd, buf, bufLen);
}



// -----------------------------------------------------------------------------
//
// notificationResponseRead - FIXME: to its own module
//
bool notificationResponseRead
(
  NotificationPending* npP,
  char*                buf,
  int                  bufLen,
  int*                 httpStatusCodeP,
  int*                 contentLengthP,
  char**               headersP,
  char**               bodyP,
  double               notificationTime
)
{
  //
  // 1. Do an initial read, with buf and bufLen
  // 2. Make sure we have headerBodyDelimiter
  //    - if 100% of body, we're done
  //    - else, read more
  //      - if timeout, return error
  //
  int      contentLen  = -1;
  int      httpStatus  = -1;
  char*    headers     = NULL;
  char*    body        = NULL;
  ssize_t  bytesRead   = 0;

  //
  // Initial read
  //
  bytesRead = readWithTimeout(npP->fd, buf, bufLen - 1, 0, 100000);

  if (bytesRead <= 0)
  {
    notificationFailure(npP->subP, "Unable to read from notification endpoint", notificationTime);
    KT_E("Internal Error (%s: unable to read response for notification on fd %d)", npP->subP->subId, npP->fd);
    return false;
  }
  buf[bytesRead] = 0;

  //
  // Find the Header/Body delimiter
  // Read more if necessary
  //
  char* headerBodyDelimiterP = strstr(buf, "\r\n\r\n");

  if (headerBodyDelimiterP == NULL)
    headerBodyDelimiterP = strstr(buf, "\n\n");

  if (headerBodyDelimiterP == NULL)
  {
    // Read mode
    int nb = readWithTimeout(npP->fd, &buf[bytesRead], bufLen - bytesRead, 0, 100000);  // 100 millisecond timeout
    if (nb == 0)
    {
      KT_W("%s: the read of notification response timed out", npP->subP->subId);
      notificationFailure(npP->subP, "timeout reading the notification response", notificationTime);
      return false;
    }
    else if (nb == -1)
    {
      char errorString[512];
      snprintf(errorString, sizeof(errorString), "error reading notification response: %s", strerror(errno));
      KT_E("%s: %s", npP->subP->subId, errorString);
      notificationFailure(npP->subP, errorString, notificationTime);
      return false;
    }

    bytesRead += nb;

    // Try to find the Header/Body delimiter again
    headerBodyDelimiterP = strstr(buf, "\r\n\r\n");
    if (headerBodyDelimiterP == NULL)
      headerBodyDelimiterP = strstr(buf, "\n\n");

    if (headerBodyDelimiterP == NULL)
    {
      KT_W("Can't find the Headers/Body delimiter");
      notificationFailure(npP->subP, "Can't find the Headers/Body delimiter", notificationTime);
      return false;
    }
  }

  // Zero delimit the Header/Body Delimiter
  *headerBodyDelimiterP = 0;

  // Step over all \r | \n  and assign the bodyP
  ++headerBodyDelimiterP;

  while ((*headerBodyDelimiterP == '\r') || (*headerBodyDelimiterP == '\n'))
  {
    ++headerBodyDelimiterP;
  }

  body = headerBodyDelimiterP;

  //
  // Find the end of the Start-Line and zero-terminate it
  // Make headersP reference what comes after it
  //
  char* cP = strstr(buf, "\r\n");
  if (cP == NULL)
  {
    cP = strstr(buf, "\n");
    if (cP != NULL)
    {
      *cP = 0;
      ++cP;
    }
  }
  else
  {
    *cP = 0;
    cP += 2;  // Jump over the '\r' and '\n'
  }

  if (cP == NULL)
  {
    KT_W("%s: Can't find the end of the Start-Line", npP->subP->subId);
    notificationFailure(npP->subP, "Can't find the end of the Start-Line", notificationTime);
    return false;
  }

  headers = cP;
  KT_T(KtNotificationMsg, "%s: notification response Start-Line:   '%s'", npP->subP->subId, buf);
  KT_T(KtNotificationMsg, "%s: notification response HTTP Headers: '%s'", npP->subP->subId, headers);
  KT_T(KtNotificationMsg, "%s: notification response body so far: '%s'", body);


  //
  // Extract the Status Code from the first line
  //
  httpStatus = statusCodeExtract(buf);

  // Extract the Content-Length (if not part of the headers and status code is not 204, it's considered an error)
  char* contentLenP = strstr(headers, "Content-Length:");
  if (contentLenP == NULL)
  {
    // Error if not 204
    if (httpStatus != 204)
    {
      KT_W("Content-Length not found but the status code (%d) is not a 204", httpStatus);
      notificationFailure(npP->subP, "Content-Length not found but the status code is not a 204", notificationTime);
      return false;
    }
    contentLen = 0;
  }
  else
    contentLen = atoi(&contentLenP[16]);

  KT_T(KtNotificationMsg, "%s: Content-Length: %d", npP->subP->subId, contentLen);


  //
  // Make sure we've read the entire body
  //
  ssize_t headersLen    = (ssize_t) headerBodyDelimiterP - (ssize_t) buf;  // Including the Start-Line
  ssize_t bodyBytesRead = bytesRead - headersLen;

  KT_T(KtNotificationMsg, "%s: total no of bytes read: %d", npP->subP->subId, bytesRead);
  KT_T(KtNotificationMsg, "%s: no of bytes of body read: %d", npP->subP->subId, bodyBytesRead);

  if (bodyBytesRead < contentLen)
  {
    //
    // We need to read more
    // Do we have room enough in 'buf'?
    // If not, we'll have to allocate more (using kaAlloc)
    //
    int bodyBytesStillToRead = contentLen - bodyBytesRead;

    if (bytesRead + bodyBytesStillToRead >= bufLen)
    {
      KT_T(KtNotificationMsg, "%s: must reallocate for the response body (we have %d bytes left in buffer, need %d)",
           npP->subP->subId,
           bufLen - bytesRead,
           bodyBytesStillToRead);

      char* newBody = kaAlloc(&orionldState.kalloc, contentLen + 1);
      if (newBody == NULL)
      {
        KT_E("Unable to allocate %d bytes for notification response body", contentLen + 1);
        notificationFailure(npP->subP, "Unable to allocate buffer for notification response body", notificationTime);
        return false;
      }

      // Copy what's already read, from body to newBody
      strncpy(newBody, body, bodyBytesRead + 1);
      body = newBody;

      // Read the rest of the body
      int nb = readWithTimeout(npP->fd, &newBody[bodyBytesRead], contentLen - bodyBytesRead, 0, 100000);  // 100 millisecond timeout
      if (nb == 0)
      {
        KT_W("The read timed out");
        notificationFailure(npP->subP, "timeout while reading notification response", notificationTime);
        return false;
      }
      else if (nb == -1)
      {
        KT_E("Other error reading (%s)", strerror(errno));
        return false;
      }

      bodyBytesRead += nb;

      if (bodyBytesRead != contentLen)
      {
        KT_W("Still not enough bytes read for the notification response body. I give up");
        notificationFailure(npP->subP, "Unable to read the entire response", notificationTime);
        return false;
      }
    }
  }

  KT_T(KtNotificationMsg, "%s: entire message read", npP->subP->subId);
  *headersP        = headers;
  *bodyP           = body;
  *httpStatusCodeP = httpStatus;

  return true;
}



// -----------------------------------------------------------------------------
//
// notificationResponseTreat -
//
static void notificationResponseTreat(NotificationPending* npP, double notificationTime)
{
  char  buf[2048];  // should be enough for the HTTP headers ...
  int   contentLength  = -1;
  int   httpStatusCode = -1;
  char* body           = NULL;
  char* headers        = NULL;
  char* subId          = npP->subP->subId;

  bzero(buf, sizeof(buf));

  if (notificationResponseRead(npP, buf, sizeof(buf), &httpStatusCode, &contentLength, &headers, &body, notificationTime) == false)
  {
    // if notificationResponseRead() returns false, it has invoked notificationFailure()
    return;
  }

  if (ktTraceLevelCheck(KtNotificationHeaders) == true)
  {
    char* headerP = headers;
    char* eol;
    while ((eol = strstr(headerP, "\n")) != NULL)
    {
      *eol = 0;
      KT_T(KtNotificationHeaders, "%s: Notification Response HTTP Header: '%s'", subId, headerP);
      headerP = &eol[1];
    }

    KT_T(KtNotificationHeaders, "%s: Notification Response HTTP Header: '%s'", subId, headerP);
  }

  KT_T(KtNotificationBody, "%s: Notification Response Body: '%s'", subId, body);

  //
  // Any 2xx response is considered OK
  //
  if (httpStatusCode == -1)
  {
    notificationFailure(npP->subP, "HTTP Start-Line of notification response not found", notificationTime);
    KT_E("Internal Error (%s:  HTTP Start-Line of notification response not found)", subId);
  }
  else if ((httpStatusCode < 200) || (httpStatusCode >= 300))
  {
    notificationFailure(npP->subP, "non 2xx response to notification", notificationTime);
    KT_E("Internal Error (%s: non 2xx response (%d) to notification on fd %d)", subId, httpStatusCode, npP->fd);
  }
  else
    notificationSuccess(npP->subP, notificationTime);
}



// -----------------------------------------------------------------------------
//
// orionldAlterationName - FIXME: Move to orionld/types/OrionldAlteration.cpp
//
const char* orionldAlterationName(OrionldAlterationType alterationType)
{
  switch (alterationType)
  {
  case EntityCreated:                return "EntityCreated";
  case EntityDeleted:                return "EntityDeleted";
  case EntityModified:               return "EntityModified";
  case AttributeAdded:               return "AttributeAdded";
  case AttributeDeleted:             return "AttributeDeleted";
  case AttributeValueChanged:        return "AttributeValueChanged";
  case AttributeMetadataChanged:     return "AttributeMetadataChanged";
  case AttributeModifiedAtChanged:   return "AttributeModifiedAtChanged";
  }

  return "Unclear Reason";
}



// -----------------------------------------------------------------------------
//
// notificationLookupByCurlHandle -
//
static NotificationPending* notificationLookupByCurlHandle(NotificationPending* notificationList, CURL* curlHandleP)
{
  for (NotificationPending* npP = notificationList; npP != NULL; npP = npP->next)
  {
    if (npP->used == true)
      continue;

    if (npP->curlHandleP == curlHandleP)
      return npP;
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// orionldAlterationsTreat -
//
// As a first "draft", just find subs in the sub-cache and send notifications, one by one
// Later - build lists of notifications and make sure one single notification in sent to each subscriber
// Also, instead of searching in the sub-cache, search in the DB, if the sub-cache is OFF
//
//
// IMPLEMENTATION DETAILS
//   I will need a new subCacheMatch function (subCacheAlterationMatch) - that accepts altList as input and gives back a list of
//   [ { SubCacheItem*, OrionldAttributeAlteration* }, {} ]
//
//   Cause, OrionldAttributeAlteration has the OrionldAlterationType, and we need it
//
//   Also, we need the complete entity as queried from DB and merged with the new, so we can include it in the notification
//   Remember - a PUT doesn't need to query the DB before replacing, but is already contains the entire entity - we need also
//   createdAt and modifiedAt, in case the subscription asks for sysAttrs
//
//   I added a field "KjNode* entityP" in struct OrionldAlteration.
//   This KjNode pointer needs to reference the entity as it is AFTER the alteration.
//
void orionldAlterationsTreat(OrionldAlteration* altList)
{
  // <DEBUG>
  if (ktTraceLevelCheck(KtAlt))
  {
    int alterations = 0;
    for (OrionldAlteration* aP = altList; aP != NULL; aP = aP->next)
    {
      KT_T(KtAlt, " Alteration %d:", alterations);
      KT_T(KtAlt, "   Entity In:      %p", aP->inEntityP);
      KT_T(KtAlt, "   CompleteEntity: %p", aP->finalApiEntityP);
      KT_T(KtAlt, "   Entity Id:      %s", aP->entityId);
      KT_T(KtAlt, "   Entity Type:    %s", aP->entityType);
      KT_T(KtAlt, "   Attributes:     %d", aP->alteredAttributes);

      for (int ix = 0; ix < aP->alteredAttributes; ix++)
      {
        KT_T(KtAlt, "   Attribute        %s", aP->alteredAttributeV[ix].attrName);
        KT_T(KtAlt, "   Alteration Type: %s", orionldAlterationType(aP->alteredAttributeV[ix].alterationType));
      }

      // KT_TREE(aP->inEntityP, "ALT:   inEntityP", KtAlt);  // outdeffed
      ++alterations;
    }
    KT_T(KtAlt, " %d Alterations present", alterations);
  }
  // </DEBUG>

  OrionldAlterationMatch* matchList;
  int                     matches;

  matchList = subCacheAlterationMatch(altList, &matches);
  if (matchList == NULL)
    return;


  //
  // The matches are ordered, grouped by subscriptionId
  // So, before notificationSend is called, the first "group" is removed from the matchList
  // to create a new list and that new list is sent to notificationSend
  //
  NotificationPending* notificationList = NULL;

  if (ktTraceLevelCheck(KtAlt) == true)
  {
    int ix = 1;
    KT_T(KtAlt, "%d items in matchList:", matches);
    for (OrionldAlterationMatch* matchP = matchList; matchP != NULL; matchP = matchP->next)
    {
      if (matchP->altAttrP != NULL)
        KT_T(KtAlt, "o %d/%d Subscription '%s', due to '%s'",
             ix,
             matches,
             matchP->subP->subId,
             orionldAlterationName(matchP->altAttrP->alterationType));
      else
        KT_T(KtAlt, "o %d/%d Subscription '%s'", ix, matches, matchP->subP->subId);

      ++ix;
    }
  }

  //
  // Applying the PATCH, if any
  //
  // FIXME:
  //   The original db entity is needed only to apply the patch.
  //   Would be better to apply the patch before so we would not need "altList->dbEntityP"
  //


  //
  // Only orionldPatchEntity2 uses this ... Right?
  // Instead of having this function create finalApiEntityP, it should be done by the service routine
  // And, after that, altList->dbEntityP is no longer needed
  //
  // FIXME: Make orionldPatchEntity2 create the finalApiEntity itself !!!
  //
  if ((altList->dbEntityP != NULL) && (altList->finalApiEntityP == NULL))
  {
    // Body-level datasetId instances were moved out of the attributes into
    // orionldState.datasets (dbModelFromApiAttributeDatasetArray). When such
    // instances exist, fold them back into dbEntityP and use dbModelToApiEntity2
    // - it (unlike the 3-arg dbModelToApiEntity) merges the @datasets instances
    // back into their attribute, so a datasetId-scoped subscription has the
    // changed dataset instance to project to.
    //
    // ONLY for requests that actually carry dataset instances: dbModelToApiEntity2
    // renders attribute names differently from the 3-arg dbModelToApiEntity (which
    // the rest of the notification pipeline depends on - e.g. the NGSIv2 formats),
    // so for the common no-datasetId case we must keep the original converter.
    if (orionldState.datasets != NULL)
    {
      // Merge the patch's dataset instances into dbEntityP's @datasets so the rendered
      // notification carries the PATCHED dataset values (not the stale DB values), using
      // the same per-instance merge that orionldPatchEntity2 applies to the DB write.
      // datasetInstancesMerge returns a fresh array and never touches its inputs - so the
      // shared orionldState.datasets (consumed downstream by the @datasets mongo write and
      // TRoE) is left intact, and dbModelToApiEntity2/datasetExtract is free to shred the
      // merged copy we install here.
      //
      KjNode* dbDatasetsP = kjLookup(altList->dbEntityP, "@datasets");
      if (dbDatasetsP == NULL)
      {
        dbDatasetsP = kjObject(orionldState.kjsonP, "@datasets");
        kjChildAdd(altList->dbEntityP, dbDatasetsP);
      }

      for (KjNode* attrDatasetP = orionldState.datasets->value.firstChildP; attrDatasetP != NULL; attrDatasetP = attrDatasetP->next)
      {
        KjNode* dbAttrArrayP = kjLookup(dbDatasetsP, attrDatasetP->name);
        KjNode* mergedP      = datasetInstancesMerge(dbAttrArrayP, attrDatasetP);

        if (dbAttrArrayP != NULL)
          kjChildRemove(dbDatasetsP, dbAttrArrayP);

        mergedP->name = attrDatasetP->name;
        kjChildAdd(dbDatasetsP, mergedP);
      }

      altList->finalApiEntityP = dbModelToApiEntity2(altList->dbEntityP, false, RF_NORMALIZED, NULL, false, &orionldState.pd);
    }
    else
      altList->finalApiEntityP = dbModelToApiEntity(altList->dbEntityP, false, altList->entityId);

    int patchNo = 0;
    for (KjNode* patchP = altList->inEntityP->value.firstChildP; patchP != NULL; patchP = patchP->next)
    {
      ++patchNo;
      orionldPatchApply(altList->finalApiEntityP, patchP, true);
    }
  }

  //
  // Timestamp for sending of notification - for stats in subscriptions
  // The request time is used instead of calling the system to ask for a new time
  //
  double notificationTime = orionldState.requestTime;

  while (matchList != NULL)
  {
    //
    // 1. Extract first match from matchList, call it matchHead
    // 2. Extract all subsequent matches from matchList - add them to matchHead
    // 3. Call notificationSend, which will combine all of them into one single notification
    //
    // The matches come in subscription order, so, we just need to break after the first non-same subscription
    //
    OrionldAlterationMatch* matchHead = matchList;

    matchList = matchList->next;
    matchHead->next = NULL;  // The matchHead-list is now NULL-terminated and separated from matchList

    while ((matchList != NULL) && (matchList->subP == matchHead->subP))
    {
      OrionldAlterationMatch* current = matchList;

      matchList     = matchList->next;
      current->next = matchHead;
      matchHead     = current;
    }

    CURL* curlHandleP = NULL;
    int   fd          = notificationSend(matchHead, notificationTime, &curlHandleP);  // curl handle as output param?

    //
    // -1 is ERROR, -2 is OK for HTTPS, anything else is a file descriptor to read from, except if MQTT.
    // For MQTT, the notificationSuccess/Failure is already taken care of by mqttNotify and nothing to read.
    // Should not be in this list.
    //
    if ((fd != -1) && (matchHead->subP->protocol != MQTT) && (matchHead->subP->protocol != WS))
    {
      NotificationPending* npP = (NotificationPending*) kaAlloc(&orionldState.kalloc, sizeof(NotificationPending));

      npP->subP        = matchHead->subP;
      npP->fd          = fd;
      npP->curlHandleP = curlHandleP;
      npP->used        = false;
      npP->next        = notificationList;
      notificationList = npP;
    }
  }

  // Start HTTPS notifications
  bool curlError = false;
  if (orionldState.multiP != NULL)
  {
    int        activeNotifications;
    CURLMcode  cm;

    KT_T(KtNotificationSend, "Starting HTTPS notifications");
    cm = curl_multi_perform(orionldState.multiP, &activeNotifications);
    if (cm != 0)
    {
      KT_E("Error starting HTTPS notifications: curl_multi_perform: error %d", cm);
      curlError = true;
    }
  }
  else
    KT_T(KtNotificationSend, "No HTTPS notifications");

  //
  // Await HTTP responses and update subscriptions accordingly (in sub cache)
  //
  int             fds;
  fd_set          rFds;
  int             fdMax      = 0;
  int             startTime  = time(NULL);
  bool            timeout    = false;

  while (timeout == false)
  {
    NotificationPending* npP = notificationList;

    //
    // Set File Descriptors for the select
    //
    fdMax = 0;
    FD_ZERO(&rFds);
    while (npP != NULL)
    {
      if (npP->fd >= 0)  // Only HTTP notifications
      {
        FD_SET(npP->fd, &rFds);
        fdMax = K_MAX(fdMax, npP->fd);
      }
      npP = npP->next;
    }

    if (fdMax == 0)
      break;

    struct timeval  tv = { 1, 0 };
    fds = select(fdMax + 1, &rFds, NULL, NULL, &tv);
    if (fds == -1)
    {
      if (errno != EINTR)
        KT_X(1, "select error: %s\n", strerror(errno));
    }

    while (fds > 0)
    {
      npP = notificationList;
      while (npP != NULL)
      {
        if ((npP->fd >= 0) && (FD_ISSET(npP->fd, &rFds)))  // Only HTTP notifications
        {
          notificationResponseTreat(npP, notificationTime);

          close(npP->fd);
          --fds;

          npP->fd   = -1;
          npP->used = true;

          break;
        }

        npP = npP->next;
      }
    }

    time_t now = time(NULL);
    if (now > startTime + 4)
    {
      timeout = true;
      break;
    }
  }

  //
  // Find timed out HTTP notification responses
  //
  NotificationPending* npP = notificationList;
  while (npP != NULL)
  {
    if (npP->fd >= 0)
    {
      // No response
      if (strncmp(npP->subP->protocolString, "mqtt", 4) != 0)
      {
        notificationFailure(npP->subP, "Timeout awaiting response from notification endpoint", notificationTime);

        KT_T(KtNotificationSend, "Closing fd %d after timeout", npP->fd);
        close(npP->fd);

        npP->fd   = -1;
        npP->used = true;
      }
    }

    npP = npP->next;
  }

  if (curlError == true)
  {
    KT_E("Something went wrong with a HTTPS notification");
    return;
  }

  //
  // Await HTTPS notification responses
  //
  if (orionldState.multiP != NULL)
  {
    KT_T(KtNotificationSend, "Awaiting HTTPS notification responses");
    int activeNotifications = 1;

    while (activeNotifications != 0)
    {
      CURLMcode  cm;

      cm = curl_multi_perform(orionldState.multiP, &activeNotifications);
      if (cm != 0)
      {
        KT_E("curl_multi_perform: error %d", cm);
        curlError = true;
        break;
      }
      else
        KT_T(KtNotificationSend, "%d HTTPS notifications are still active", activeNotifications);

      if (activeNotifications > 0)
      {
        //
        // The curl library of the UBI base image doesn't have "curl_multi_poll".
        // Using curl_multi_wait instead.
        // For now, at least
        //
        cm = curl_multi_wait(orionldState.multiP, NULL, 0, 1000, NULL);
        if (cm != 0)
          KT_E("curl_multi_wait error %d", cm);
      }
    }

    // Read the results of the notifications
    CURLMsg* msgP;
    int      msgsLeft;

    while ((msgP = curl_multi_info_read(orionldState.multiP, &msgsLeft)) != NULL)
    {
      if (msgP->msg != CURLMSG_DONE)
        continue;

      NotificationPending* npP = notificationLookupByCurlHandle(notificationList, msgP->easy_handle);

      if (npP == NULL)
      {
        KT_W("No 'Pending Notification' found for a curl easy handle");
        continue;
      }

      KT_T(KtNotificationSend, "%s: Notification Host: '%s'", npP->subP->subId, npP->subP->ip);
      KT_T(KtNotificationSend, "%s: Notification Result: CURLcode %d (%s)", npP->subP->subId, msgP->data.result, curl_easy_strerror(msgP->data.result));
      KT_T(KtNotificationSend, "%s: Update Counters", npP->subP->subId);

      if (msgP->data.result == 0)
      {
        uint64_t  httpResponseCode = 500;
        curl_easy_getinfo(npP->curlHandleP, CURLINFO_RESPONSE_CODE, &httpResponseCode);

        KT_T(KtNotificationSend, "%s: Notification Response HTTP Status: %d", npP->subP->subId, (int) httpResponseCode);

        if ((httpResponseCode >= 200) && (httpResponseCode < 300))
          notificationSuccess(npP->subP, notificationTime);
        else
        {
          char errorString[256];

          snprintf(errorString, sizeof(errorString), "Got an HTTP Status %d", (int) httpResponseCode);
          notificationFailure(npP->subP, errorString, notificationTime);
        }
      }
      else
      {
        char errorString[512];

        snprintf(errorString, sizeof(errorString), "CURL Error %d: %s", msgP->data.result, curl_easy_strerror(msgP->data.result));
        notificationFailure(npP->subP, errorString, notificationTime);
      }

      npP->used = true;
    }
  }

  for (NotificationPending* npP = notificationList; npP != NULL; npP = npP->next)
  {
    if (npP->used == true)
      continue;

    if (npP->subP->protocol == MQTT || npP->subP->protocol == MQTTS)
      continue;

    notificationFailure(npP->subP, "Notification never reached its destination", notificationTime);
  }
}
