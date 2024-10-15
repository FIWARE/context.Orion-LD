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
extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "ktrace/ktGlobals.h"                               // globals for KT library
#include "kjson/KjNode.h"                                   // KjNode
#include "kjson/kjRender.h"                                 // kjRender
#include "kjson/kjRenderSize.h"                             // kjRenderSize
#include "kjson/kjParse.h"                                  // kjParse
#include "kjson/kjBuilder.h"                                // kjObject, kjArray, kjString, kjChildAdd, ...
#include "kjson/kjFree.h"                                   // kjFree
#include "kjson/kjLookup.h"                                 // kjLookup
#include "kjson/kjClone.h"                                  // kjClone
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // Trace levels for ktrace
#include "dds/kjTreeLog.h"                                  // kjTreeLog2

#include "ftClient/mhdRequestTreat.h"                       // Own interface

// Service Routines
#include "ftClient/getDump.h"                               // getDump
#include "ftClient/deleteDump.h"                            // deleteDump
#include "ftClient/die.h"                                   // die
#include "ftClient/deleteDdsDump.h"                         // deleteDdsDump
#include "ftClient/getDdsDump.h"                            // getDdsDump
#include "ftClient/postDdsPub.h"                            // postDdsPub
#include "ftClient/postDdsSub.h"                            // postDdsSub


extern __thread KjNode* uriParams;    // These two need to go inside orionldState ...
extern __thread KjNode* httpHeaders;  // Perhaps a callback to reach headerReceive+uriParamReceive from ftClient.cpp


KjNode*                 dumpArray    = NULL;
KjNode*                 ddsDumpArray = NULL;



// -----------------------------------------------------------------------------
//
// FtService -
//
typedef KjNode* (*FtTreat)(int* statusCodeP);
typedef struct FtService
{
  Verb             verb;
  const char*      url;
  FtTreat          treatP;
} FtService;



// -----------------------------------------------------------------------------
//
// serviceV -
//
FtService serviceV[] =
{
  { HTTP_GET,      "/dump",      getDump       },
  { HTTP_DELETE,   "/dump",      deleteDump    },
  { HTTP_GET,      "/die",       die           },
  { HTTP_POST,     "/dds/sub",   postDdsSub    },
  { HTTP_POST,     "/dds/pub",   postDdsPub    },
  { HTTP_GET,      "/dds/dump",  getDdsDump    },
  { HTTP_DELETE,   "/dds/dump",  deleteDdsDump },
  { HTTP_NOVERB,   NULL,         NULL          }
};



__thread char*          responseText    = NULL;  // FIXME: use something inside orionldState instead ... ?
// -----------------------------------------------------------------------------
//
// mhdRequestTreat -
//
char* mhdRequestTreat(int* statusCodeP)
{
  KT_T(StRequest, "In mhdRequestTreat");
  int ix   = 0;

  // Parse the incoming payload body, if present
  if (orionldState.in.payload != NULL)
  {
    KT_T(StRequest, "Parsing incoming payload body '%s'", orionldState.in.payload);
    orionldState.requestTree = kjParse(orionldState.kjsonP, orionldState.in.payload);
    KT_T(StRequest, "payloadTree at %p ", orionldState.requestTree);
  }

  // Lookup the service routine and execute it
  KT_T(StRequest, "Looking up the service routine (%s %s)", orionldState.verbString, orionldState.urlPath);
  while (serviceV[ix].treatP != NULL)
  {
    if ((orionldState.verb == serviceV[ix].verb) && (strcmp(orionldState.urlPath, serviceV[ix].url) == 0))
    {
      KT_T(StRequest, "Found the service routine");
      KjNode* responseTree = serviceV[ix].treatP(statusCodeP);
      KT_T(StRequest, "Ran the service routine, response tree at %p", responseTree);

      if (responseTree == NULL)
        return responseText;

      int     bufSize      = kjRenderSize(orionldState.kjsonP, responseTree) + 1024;
      char*   buf          = kaAlloc(&orionldState.kalloc, bufSize);

      kjRender(orionldState.kjsonP, responseTree, buf, bufSize);
      return buf;
    }

    ++ix;
  }
  KT_T(StRequest, "No service routine found - accumulating");

  // Service not found - accumulate
  KjNode* dump    = kjObject(NULL, "item");
  KjNode* verbP   = kjString(NULL, "verb", orionldState.verbString);
  KjNode* path    = kjString(NULL, "url",  orionldState.urlPath);

  kjChildAdd(dump, verbP);
  kjChildAdd(dump, path);

  if (uriParams != NULL)
    kjChildAdd(dump, uriParams);
  if (httpHeaders != NULL)
    kjChildAdd(dump, httpHeaders);

  if (orionldState.requestTree != NULL)
  {
    KjNode* requestTree = kjClone(NULL, orionldState.requestTree);
    requestTree->name = (char*) "body";
    kjChildAdd(dump, requestTree);
  }

  if (dumpArray == NULL)
  {
    KT_T(StDump, "Creating the dump array");
    dumpArray = kjArray(NULL, "dumpArray");
  }

  kjTreeLog2(dump, "Adding to dump array", StDump);
  kjChildAdd(dumpArray, dump);

  *statusCodeP = 200;

  return (char*) "";
}
