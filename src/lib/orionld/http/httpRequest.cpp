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
#include <curl/curl.h>                                         // curl

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjRender.h"                                    // kjFastRender
#include "kjson/kjRenderSize.h"                                // kjFastRenderSize
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/traceLevels.h"                        // KTrace levels
#include "orionld/types/OrionldProblemDetails.h"               // OrionldProblemDetails
#include "orionld/types/OrionldResponseBuffer.h"               // OrionldResponseBuffer
#include "orionld/http/httpRequest.h"                          // HttpKeyValue, Own interface



// -----------------------------------------------------------------------------
//
// curlDebug - from orionldRequestSend.cpp
//
extern int curlDebug(CURL* handle, curl_infotype type, char* data, size_t size, void* userptr);



// -----------------------------------------------------------------------------
//
// writeCallback -
//
static size_t writeCallback(void* contents, size_t size, size_t members, void* userP)
{
  size_t                  bytesToCopy  = size * members;
  OrionldResponseBuffer*  rBufP        = (OrionldResponseBuffer*) userP;
  int                     xtraBytes    = 512;

  KT_T(KtCurl, "CURL: got %d bytes of payload body: %s", bytesToCopy, contents);

  if (bytesToCopy + rBufP->used >= rBufP->size)
  {
    if (rBufP->buf == rBufP->internalBuffer)
    {
      rBufP->buf  = (char*) malloc(rBufP->size + bytesToCopy + xtraBytes);

      if (rBufP->buf == NULL)
        KT_X(1, "Runtime Error (out of memory)");

      rBufP->size = rBufP->size + bytesToCopy + xtraBytes;

      if (rBufP->used > 0)  // Copy contents from internal buffer that got too small
      {
        memcpy(rBufP->buf, rBufP->internalBuffer, rBufP->used);
      }
    }
    else
    {
      rBufP->buf = (char*) realloc(rBufP->buf, rBufP->size + bytesToCopy + xtraBytes);
      rBufP->size = rBufP->size + bytesToCopy + xtraBytes;
    }

    if (rBufP->buf == NULL)
      KT_X(1, "Runtime Error (out of memory)");

    //
    // Save pointer to allocated buffer for later call to free()
    //
    orionldState.delayedFreePointer = rBufP->buf;
  }

  memcpy(&rBufP->buf[rBufP->used], contents, bytesToCopy);

  rBufP->used += bytesToCopy;
  rBufP->buf[rBufP->used] = 0;

  return bytesToCopy;
}



// -----------------------------------------------------------------------------
//
// responseHeaderDebug -
//
static size_t responseHeaderRead(char* buffer, size_t size, size_t nitems, void* userdata)
{
  int* statusCodeP = (int*) userdata;

  if (*statusCodeP < 200)  // Status code Not set
  {
    if (strncmp(buffer, "HTTP/", 5) == 0)
    {
      // extract status code
      char* space = strchr(buffer, ' ');
      if (space != 0)
      {
        ++space;
        *statusCodeP = atoi(space);
      }
    }
  }

  KT_T(KtDistOpResponseHeaders, "Response Header: %s", buffer);
  return nitems;
}



// -----------------------------------------------------------------------------
//
// httpRequest -
//
// RETURN VALUE
//   -1 on fatal error (error info saved in pdP)
//   
// PARAMETERS
//   verb:
//   ip:
//   urlIn:     with or without URL parameters (if 'uriParams' == NULL, it is used "as is")
//   body:      request payload body as KjNode tree
//   uriParams: URI parameters, as an array of HttpKeyValue - NULL if "url" already contains the URL parameters
//   headers:   HTTP Headers, as an array of HttpKeyValue - both input and output
//   tmo:       Timeout of reading the response? in milliseconds
//   response:  response payload body, as a KjNode tree
//   pdP:       OrionldProblemDetails reference
//
int httpRequest
(
  const char*             ip,
  const char*             verb,
  const char*             urlIn,
  KjNode*                 body,
  HttpKeyValue*           uriParams,
  HttpKeyValue*           headers,
  int                     tmo,
  KjNode**                responseTree,
  OrionldProblemDetails*  pdP
)
{
  int                  httpStatus = 500;
  char*                url       = (char*) urlIn;

  *responseTree = NULL;
  if (uriParams != NULL)
  {
    int len = 0;

    for (int ix = 0; uriParams[ix].key != NULL; ix++)
    {
      KT_T(KtDistOpRequestParams, "URI param: %s=%s", uriParams[ix].key, uriParams[ix].value);
      len += strlen(uriParams[ix].key) + 2 + strlen(uriParams[ix].value);  // 2: =&
    }

    len += 1 + strlen(urlIn) + 1;  // 1:? 1:\0

    url = kaAlloc(&orionldState.kalloc, len);
    if (url == NULL)
    {
      pdP->title  = (char*) "Out of memory";
      pdP->detail = (char*) "trying to allocate memory for full URL (including URL Params)";
      KT_RE(-1, "%s: %s - total length: %d", pdP->title, pdP->detail, len);
    }

    snprintf(url, len, "%s?", urlIn);

    for (int ix = 0; uriParams[ix].key != NULL; ix++)
    {
      strcat(url, uriParams[ix].key);
      strcat(url, "=");
      strcat(url, uriParams[ix].value);
      if (uriParams[ix+1].key != NULL)
        strcat(url, "&");
    }
  }

  struct curl_context  cc;
  struct curl_slist*   curlHeaders = NULL;
  CURLcode             cCode;

  get_curl_context(ip, &cc);
  if (cc.curl == NULL)
  {
    pdP->title   = (char*) "Unable to obtain CURL context";
    pdP->detail  = (char*) "FIXME: get the curl error string";
    pdP->status  = 500;

    KT_RE(-1, "Internal Error (Unable to obtain CURL context)");
  }

  //
  // Is curl to be debugged?
  //
  if (debugCurl == true)
  {
    curl_easy_setopt(cc.curl, CURLOPT_VERBOSE,       1L);
    curl_easy_setopt(cc.curl, CURLOPT_DEBUGFUNCTION, curlDebug);
  }

  //
  // Prepare the CURL handle
  //
  OrionldResponseBuffer rBuf;
  int                   responseCode = -1;

  bzero(&rBuf, sizeof(rBuf));

  curl_easy_setopt(cc.curl, CURLOPT_URL, url);                             // Set the URL Path
  curl_easy_setopt(cc.curl, CURLOPT_CUSTOMREQUEST, verb);                  // Set the HTTP verb
  curl_easy_setopt(cc.curl, CURLOPT_WRITEFUNCTION, writeCallback);         // Callback function for writes
  curl_easy_setopt(cc.curl, CURLOPT_WRITEDATA, &rBuf);                     // Custom data for response handling
  curl_easy_setopt(cc.curl, CURLOPT_TIMEOUT_MS, tmo);                      // Timeout, in milliseconds
  curl_easy_setopt(cc.curl, CURLOPT_FAILONERROR, true);                    // Fail On Error - to detect 404 etc.
  curl_easy_setopt(cc.curl, CURLOPT_FOLLOWLOCATION, 1L);                   // Follow redirections
  curl_easy_setopt(cc.curl, CURLOPT_HEADERFUNCTION, responseHeaderRead);  // Callback for headers
  curl_easy_setopt(cc.curl, CURLOPT_HEADERDATA,     &responseCode);        // Callback data for headers

  if (body != NULL)
  {
    unsigned long bodySize       = kjFastRenderSize(body) + 64;
    char*         serializedBody = kaAlloc(&orionldState.kalloc, bodySize);

    kjFastRender(body, serializedBody);
    KT_T(KtDistOpRequest, "Body for HTTP request: '%s'", serializedBody);
    curl_easy_setopt(cc.curl, CURLOPT_POSTFIELDS, (u_int8_t*) serializedBody);

    int  contentLen = strlen(serializedBody);
    char ibuf[64];

    snprintf(ibuf, sizeof(ibuf), "Content-Length:%d", contentLen);
    curlHeaders = curl_slist_append(curlHeaders, ibuf);
    KT_T(KtDistOpRequestHeaders, "Content-Length HTTP Header: %s", ibuf);
  }

  if (headers != NULL)
  {
    for (int ix = 0; headers[ix].key != NULL; ix++)
    {
      char buf[256];

      KT_T(KtDistOpRequestHeaders, "HTTP Header: %s: %s", headers[ix].key, headers[ix].value);

      snprintf(buf, sizeof(buf) - 1, "%s:%s", headers[ix].key, headers[ix].value);
      curlHeaders = curl_slist_append(curlHeaders, buf);
    }

    curl_easy_setopt(cc.curl, CURLOPT_HTTPHEADER, curlHeaders);
  }

  KT_T(KtDistOpRequest, "Sending HTTP request: %s %s", verb, url);
  cCode = curl_easy_perform(cc.curl);
  httpStatus = responseCode;

  //
  // Cleanup
  //
  if (headers != NULL)
    curl_slist_free_all(curlHeaders);

  release_curl_context(&cc);

  if (cCode != CURLE_OK)
  {
    pdP->detail  = (char*) url;
    pdP->title   = (char*) "Internal CURL Error";

    KT_RE(-1, "Internal Error (curl_easy_perform returned error code %d)", cCode);
  }

  // The downloaded buffer is in rBuf.buf - let's parse it into a KjNode tree!
  *responseTree = kjParse(orionldState.kjsonP, rBuf.buf);

  return httpStatus;
}
