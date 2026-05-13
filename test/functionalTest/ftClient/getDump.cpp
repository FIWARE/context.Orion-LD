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
#include <stdio.h>                                          // snprintf
#include <string.h>                                         // strcpy, memset
#include <strings.h>                                        // bzero
#include <pthread.h>                                        // pthread_mutex_t

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                 // kaAlloc
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjRender.h"                                 // kjRender
#include "kjson/kjRenderSize.h"                             // kjRenderSize
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjArray
#include "kjson/kjSort.h"                                   // kjSort
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // Trace levels for ktrace



// FIXME: put in header file and include
extern KjNode*          dumpArray;
extern pthread_mutex_t  dumpMutex;
extern void             dumpLockOrAbort(const char* siteTag);
extern __thread char*   responseText;
extern __thread KjNode* uriParams;
extern bool             prettyPrint;
extern char*            httpsKey;
extern char*            httpsCertificate;



// -----------------------------------------------------------------------------
//
// dumpHeaderOrder -
//
// Headers listed here come first, in the listed order. Any remaining captured
// headers follow in capture order.
//
static const char* dumpHeaderOrder[] =
{
  "Fiware-Servicepath",
  "Content-Length",
  "Authorization",
  "X-Auth-Token",
  "User-Agent",
  "Ngsiv2-Attrsformat",
  "Host",
  "Accept",
  "Fiware-Service",
  "Ngsild-Tenant",
  "Ngsild-Scope",
  "Content-Type",
  "Fiware-Correlator",
  "Link",
  NULL
};



// -----------------------------------------------------------------------------
//
// strcasecmpEq - case-insensitive equality
//
static inline bool strcasecmpEq(const char* a, const char* b)
{
  if ((a == NULL) || (b == NULL))
    return false;
  while (*a && *b)
  {
    char ca = *a;
    char cb = *b;
    if ((ca >= 'A') && (ca <= 'Z')) ca += 32;
    if ((cb >= 'A') && (cb <= 'Z')) cb += 32;
    if (ca != cb)
      return false;
    ++a;
    ++b;
  }
  return *a == *b;
}



// -----------------------------------------------------------------------------
//
// findHeader - case-insensitive lookup of header by name
//
static KjNode* findHeader(KjNode* headersP, const char* name)
{
  if (headersP == NULL)
    return NULL;
  for (KjNode* h = headersP->value.firstChildP; h != NULL; h = h->next)
  {
    if (strcasecmpEq(h->name, name))
      return h;
  }
  return NULL;
}



// -----------------------------------------------------------------------------
//
// titleCaseHeader - normalize a header name to title-case, mirroring Werkzeug.
//
// Splits on '-', capitalizes the first letter of each part, lowercases the rest.
// e.g. "Ngsiv2-AttrsFormat" -> "Ngsiv2-Attrsformat", "fiware-service" -> "Fiware-Service".
// Writes into a caller-provided buffer; safe for short header names.
//
static void titleCaseHeader(const char* in, char* out, int outSize)
{
  bool atStart = true;
  int  i = 0;
  while ((in[i] != 0) && (i < outSize - 1))
  {
    char c = in[i];
    if (c == '-')
    {
      out[i] = '-';
      atStart = true;
    }
    else if (atStart)
    {
      out[i] = ((c >= 'a') && (c <= 'z')) ? (char)(c - 32) : c;
      atStart = false;
    }
    else
    {
      out[i] = ((c >= 'A') && (c <= 'Z')) ? (char)(c + 32) : c;
    }
    ++i;
  }
  out[i] = 0;
}






// -----------------------------------------------------------------------------
//
// getDump -
//
// The dumpArray is an array (one item per request received) of the form:
// [
//   {
//     "verb": "GET",
//     "path": "/a/b/c",
//     "params": {
//       "verbose": true,
//       ...
//     },
//     "headers": {
//       "Content-Length": 125
//       "Content-Type": "application/json",
//       ...
//     },
//     "body": <PAYLOAD BODY>
//   },
//   ...
// ]
//
// BUT, the output we want is text based - like Orion-LD's accumulator
//
// So, instead of giving back the dumpArray, it is transformed into:
//
// VERB /urlPath?param1=X&param2=Y
// <HTTP Headers>
// <Empty Line>
// <BODY>
// ==============================
// VERB /urlPath?param1=X&param2=Y
// <HTTP Headers>
// <Empty Line>
// <BODY>
// ==============================
//
// The function returns NULL which makes mhdRequestTreat() use the text in 'responseText'
//
KjNode* getDump(int* statusCodeP)
{
  *statusCodeP = 200;

  dumpLockOrAbort("getDump");

  //
  // Nothing to dump?
  // - Return the empty array
  //
  if (dumpArray == NULL)
    dumpArray = kjArray(NULL, NULL);

  // Honor ?count=true and ?limit=0 (broker NGSI-LD pagination convention).
  // - count=true: emit the count of accumulated items (number on its own line)
  // - limit=0:    do not render items
  // When both are set, output is just the count. count=true alone returns count
  // followed by the dump body. limit=0 alone returns an empty body.
  bool wantCount = false;
  bool limitZero = false;
  if (uriParams != NULL)
  {
    KjNode* countP = kjLookup(uriParams, "count");
    if ((countP != NULL) && (countP->type == KjString) && (strcmp(countP->value.s, "true") == 0))
      wantCount = true;
    KjNode* limitP = kjLookup(uriParams, "limit");
    if ((limitP != NULL) && (limitP->type == KjString) && (strcmp(limitP->value.s, "0") == 0))
      limitZero = true;
  }

  if (wantCount || limitZero)
  {
    int n = 0;
    for (KjNode* p = dumpArray->value.firstChildP; p != NULL; p = p->next)
      ++n;

    char* buf = (char*) kaAlloc(&orionldState.kalloc, 32);
    if (wantCount)
      snprintf(buf, 32, "%d\n", n);
    else
      buf[0] = 0;
    responseText = buf;
    pthread_mutex_unlock(&dumpMutex);
    return NULL;
  }

  // /dump output is rendered in accumulator format:
  //   "VERB <scheme>://<Host><path>"
  //   <Title-Cased headers>
  //   <blank>
  //   <body, sorted keys, 4-space indent when prettyPrint, else compact>
  //   =======================================
  // ddsDumpArray is dumped separately via /dds/dump.
  KT_V("dumpArray at %p", dumpArray);
  if (dumpArray->value.firstChildP == NULL)
  {
    // Empty dump → empty body, matching the Python accumulator. The caller
    // (mhdRequest) bzero's responseText after MHD copies it; the kaAlloc'd
    // 1-byte buffer is writable, so bzero of length 0 is safe either way.
    char* emptyBuf = (char*) kaAlloc(&orionldState.kalloc, 1);
    emptyBuf[0] = 0;
    responseText = emptyBuf;
    pthread_mutex_unlock(&dumpMutex);
    return NULL;
  }
  KT_V("dumpArray at %p", dumpArray);

  // Compute a buffer size that accounts for 4-space pretty-printing
  int savedIndentForSizing = orionldState.kjsonP->spacesPerIndent;
  if (prettyPrint)
    orionldState.kjsonP->spacesPerIndent = 4;
  int     bufSize      = kjRenderSize(orionldState.kjsonP, dumpArray) + 16 * 1024;
  orionldState.kjsonP->spacesPerIndent = savedIndentForSizing;
  char*   buf          = kaAlloc(&orionldState.kalloc, bufSize);
  if (buf == NULL)
  {
    // Bigger than the kalloc single-allocation cap (4 MB). Fall back to malloc
    // so the dump still works and we don't crash. Caller's render is one-shot;
    // we can't easily free this — accept the small leak.
    buf = (char*) malloc(bufSize);
    if (buf == NULL)
    {
      KT_E("getDump: out of memory for %d-byte response buffer", bufSize);
      *statusCodeP = 500;
      pthread_mutex_unlock(&dumpMutex);
      return NULL;
    }
  }

  bzero(buf, bufSize);

  int bufIx = 0;
  for (KjNode* inP = dumpArray->value.firstChildP; inP != NULL; inP = inP->next)
  {
    char    line[16 * 1024];  // big to fit oversized headers (JWT Authorization etc.)
    KjNode* verbP    = kjLookup(inP, "verb");
    KjNode* urlP     = kjLookup(inP, "url");
    KjNode* paramsP  = kjLookup(inP, "params");
    KjNode* headersP = kjLookup(inP, "headers");
    KjNode* bodyP    = kjLookup(inP, "body");

    // Status Line: "VERB <scheme>://<Host><path>" — mirrors the old Python accumulator,
    // which dumped the request-line that the broker (libcurl) sent verbatim.
    int         lineLen;
    {
      KjNode*     hostP  = findHeader(headersP, "Host");
      const char* scheme = ((httpsKey != NULL) && (httpsCertificate != NULL)) ? "https" : "http";
      lineLen = snprintf(line, sizeof(line), "%s %s://%s%s",
                         verbP->value.s,
                         scheme,
                         (hostP != NULL) ? hostP->value.s : "",
                         urlP->value.s);
    }
    strcpy(&buf[bufIx], line);
    bufIx += lineLen;

    // URI Params
    if (paramsP != NULL)
    {
      for (KjNode* paramP = paramsP->value.firstChildP; paramP != NULL; paramP = paramP->next)
      {
        lineLen = snprintf(line, sizeof(line), "&%s=%s", paramP->name, paramP->value.s);
        if (paramP == paramsP->value.firstChildP)
          line[0] = '?';
        strcpy(&buf[bufIx], line);
        bufIx += lineLen;
      }
    }

    // Terminating the first line
    buf[bufIx] = '\n';
    ++bufIx;

    // HTTP Headers: predefined order first (title-cased), then remaining in insertion order
    if (headersP != NULL)
    {
      int hCount = 0;
      for (KjNode* h = headersP->value.firstChildP; h != NULL; h = h->next)
        ++hCount;

      bool* emitted = (bool*) kaAlloc(&orionldState.kalloc, sizeof(bool) * (hCount > 0 ? hCount : 1));
      for (int i = 0; i < hCount; ++i)
        emitted[i] = false;

      char nameBuf[128];

      for (int oi = 0; dumpHeaderOrder[oi] != NULL; ++oi)
      {
        int hi = 0;
        for (KjNode* h = headersP->value.firstChildP; h != NULL; h = h->next, ++hi)
        {
          if ((emitted[hi] == false) && strcasecmpEq(h->name, dumpHeaderOrder[oi]))
          {
            lineLen = snprintf(line, sizeof(line), "%s: %s\n", dumpHeaderOrder[oi], h->value.s);
            strcpy(&buf[bufIx], line);
            bufIx += lineLen;
            emitted[hi] = true;
            break;
          }
        }
      }

      int hi = 0;
      for (KjNode* h = headersP->value.firstChildP; h != NULL; h = h->next, ++hi)
      {
        if (emitted[hi] == false)
        {
          titleCaseHeader(h->name, nameBuf, sizeof(nameBuf));
          lineLen = snprintf(line, sizeof(line), "%s: %s\n", nameBuf, h->value.s);
          strcpy(&buf[bufIx], line);
          bufIx += lineLen;
        }
      }
    }

    KT_T(StRequest, "bodyP at %p", bodyP);
    // Body: blank line, sorted+indented (pretty) or compact body, trailing 39-char separator
    {
      if (bodyP != NULL)
      {
        buf[bufIx] = '\n';
        ++bufIx;

        if (bodyP->type == KjString)
        {
          // Non-JSON body captured as a raw string (e.g. text/plain notifications).
          // The body is emitted as-is — typically the broker's notification template
          // already ends in a newline; we don't add one (Python accumulator doesn't either).
          int sLen = snprintf(&buf[bufIx], bufSize - bufIx, "%s", bodyP->value.s);
          bufIx += sLen;
        }
        else
        {
          if (prettyPrint)
          {
            kjSort(bodyP);

            int savedIndent = orionldState.kjsonP->spacesPerIndent;
            orionldState.kjsonP->spacesPerIndent = 4;

            int   bodyLen = kjRenderSize(orionldState.kjsonP, bodyP) + 512;
            char* body    = kaAlloc(orionldState.kjsonP->kallocP, bodyLen);
            kjRender(orionldState.kjsonP, bodyP, body);

            orionldState.kjsonP->spacesPerIndent = savedIndent;

            // Pretty output ends with '\n' from kjRender; strip it so the 39-char
            // separator sits on its own line, and we re-add a newline below.
            int bodyStrLen = strlen(body);
            while ((bodyStrLen > 0) && (body[bodyStrLen - 1] == '\n'))
              --bodyStrLen;

            memcpy(&buf[bufIx], body, bodyStrLen);
            bufIx += bodyStrLen;
            buf[bufIx] = '\n';
            ++bufIx;
          }
          else
          {
            // Compact (no spaces, no newlines) — Python accumulator's raw body.
            int   bodyLen = kjFastRenderSize(bodyP) + 512;
            char* body    = kaAlloc(orionldState.kjsonP->kallocP, bodyLen);
            kjFastRender(bodyP, body);

            int bodyStrLen = strlen(body);
            memcpy(&buf[bufIx], body, bodyStrLen);
            bufIx += bodyStrLen;
          }
        }
      }

      memset(&buf[bufIx], '=', 39);
      bufIx += 39;
      buf[bufIx] = '\n';
      ++bufIx;
    }
  }
  buf[bufIx] = 0;

  responseText = buf;

  pthread_mutex_unlock(&dumpMutex);
  return NULL;  // => responseText is used as is
}
