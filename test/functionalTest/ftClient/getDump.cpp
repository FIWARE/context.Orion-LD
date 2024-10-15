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

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kalloc/kaAlloc.h"                                 // kaAlloc
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjRender.h"                                 // kjRender
#include "kjson/kjRenderSize.h"                             // kjRenderSize
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjBuilder.h"                                // kjArray
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // Trace levels for ktrace



// FIXME: put in header file and include
extern KjNode*          dumpArray;
extern __thread char*   responseText;



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

  //
  // Nothing to dump?
  // - Return the empty array
  //
  if (dumpArray == NULL)
    dumpArray = kjArray(NULL, NULL);

  KT_V("dumpArray at %p", dumpArray);
  if (dumpArray->value.firstChildP == NULL)
    return dumpArray;
  KT_V("dumpArray at %p", dumpArray);

  int     bufSize      = kjRenderSize(orionldState.kjsonP, dumpArray);
  char*   buf          = kaAlloc(&orionldState.kalloc, bufSize);

  bzero(buf, bufSize);

  int bufIx = 0;
  for (KjNode* inP = dumpArray->value.firstChildP; inP != NULL; inP = inP->next)
  {
    char    line[256];
    KjNode* verbP    = kjLookup(inP, "verb");
    KjNode* urlP     = kjLookup(inP, "url");
    KjNode* paramsP  = kjLookup(inP, "params");
    KjNode* headersP = kjLookup(inP, "headers");
    KjNode* bodyP    = kjLookup(inP, "body");

    // Delimiter
    if (bufIx != 0)
    {
      memset(line, '=', 80);
      line[80] = '\n';
      line[81] = '\n';
      line[82] = 0;
      strcpy(&buf[bufIx], line);
      bufIx += 82;
    }

    // Status Line
    int lineLen = snprintf(line, sizeof(line), "%s %s", verbP->value.s, urlP->value.s);
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

    // HTTP Headers
    if (headersP != NULL)
    {
      for (KjNode* headerP = headersP->value.firstChildP; headerP != NULL; headerP = headerP->next)
      {
        lineLen = snprintf(line, sizeof(line), "%s: %s\n", headerP->name, headerP->value.s);
        strcpy(&buf[bufIx], line);
        bufIx += lineLen;
      }
    }

    // Empty line
    buf[bufIx] = '\n';
    ++bufIx;

    // Payload body
    KT_T(StRequest, "bodyP at %p", bodyP);
    if (bodyP != NULL)
    {
      int   bodyLen = kjRenderSize(orionldState.kjsonP, bodyP) + 512;
      char* body    = kaAlloc(orionldState.kjsonP->kallocP, bodyLen);

      kjRender(orionldState.kjsonP, bodyP, body, bodyLen);
      KT_T(StRequest, "body: '%s'", body);
      strcpy(&buf[bufIx], body);
      bufIx += strlen(body);
    }
  }
  buf[bufIx] = 0;

  responseText = buf;

  return NULL;  // => responseText is used as is
}
