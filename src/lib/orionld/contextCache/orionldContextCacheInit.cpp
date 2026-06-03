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
#include <stdio.h>                                               // fopen, fread, fclose, fseek, ftell
#include <stdlib.h>                                              // malloc, free
#include <string.h>                                              // strrchr, strlen
#include <strings.h>                                             // bzero
#include <string.h>                                              // strncmp, strncpy

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildRemove
#include "kbase/kFileRead.h"                                     // kFileRead
}

#include "orionld/common/orionldState.h"                         // dbHost, coreContextUrl, builtinCoreContext
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/mongoc/mongocContextCacheGet.h"                // mongocContextCacheGet
#include "orionld/context/orionldCoreContext.h"                  // orionldCoreContextP, builtinCoreContextUrl, builtinCoreContext
#include "orionld/context/orionldContextFromUrl.h"               // orionldContextFromUrl
#include "orionld/context/orionldContextFromTree.h"              // orionldContextFromTree
#include "orionld/context/orionldContextFromBuffer.h"            // orionldContextFromBuffer
#include "orionld/contextCache/orionldContextCache.h"            // orionldContextCacheArray, orionldContextCacheSem
#include "orionld/contextCache/orionldContextCachePersist.h"     // orionldContextCachePersist
#include "orionld/contextCache/orionldContextCacheInit.h"        // Own interface



// -----------------------------------------------------------------------------
//
// defaultUserContextInit -
//
static void defaultUserContextInit(void)
{
  if ((strncmp(defaultUserContextUrl, "http://", 7) == 0) || (strncmp(defaultUserContextUrl, "https://", 8) == 0))
  {
    defaultUserContextP = orionldContextFromUrl(defaultUserContextUrl, NULL);
    if (defaultUserContextP == NULL)
      KT_X(1, "Unable to download the default user context '%s' (%s: %s)", defaultUserContextUrl, orionldState.pd.title, orionldState.pd.detail);
  }
  else
  {
    int bufferLen = 0;

    if (kFileRead((char*) "", defaultUserContextUrl, &defaultUserContextBuffer, &bufferLen) != 0)
      KT_X(1, "Unable to read default user context file '%s'", defaultUserContextUrl);

    char hostedUrl[256];
    snprintf(hostedUrl, sizeof(hostedUrl), "http://localhost:%d/ngsi-ld/v1/jsonldContexts/defaultUserContext.jsonld", portNo);

    defaultUserContextP = orionldContextFromBuffer(hostedUrl, OrionldContextUserCreated, NULL, defaultUserContextBuffer);
    if (defaultUserContextP == NULL)
    {
      free(defaultUserContextBuffer);
      defaultUserContextBuffer = NULL;
      KT_X(1, "Unable to parse default user context file '%s' (%s: %s)", defaultUserContextUrl, orionldState.pd.title, orionldState.pd.detail);
    }
    defaultUserContextP->kind = OrionldContextHosted;

    strncpy(defaultUserContextUrl, hostedUrl, sizeof(defaultUserContextUrl) - 1);
    defaultUserContextUrl[sizeof(defaultUserContextUrl) - 1] = 0;
  }
}



// -----------------------------------------------------------------------------
//
// dbContextToCache -
//
void dbContextToCache(KjNode* dbContextP, KjNode* atContextP, bool keyValues, bool coreContext)
{
  KjNode* idNodeP        = kjLookup(dbContextP, "_id");
  KjNode* urlNodeP       = kjLookup(dbContextP, "url");
  KjNode* parentNodeP    = kjLookup(dbContextP, "parent");
  KjNode* originNodeP    = kjLookup(dbContextP, "origin");
  KjNode* kindNodeP      = kjLookup(dbContextP, "kind");
  KjNode* createdAtNodeP = kjLookup(dbContextP, "createdAt");

  if (idNodeP        == NULL) KT_RVE("Database Error (No 'id' node in cached context in DB)");
  if (urlNodeP       == NULL) KT_RVE("Database Error (No 'url' node in cached context in DB)");
  if (originNodeP    == NULL) KT_RVE("Database Error (No 'origin' node in cached context in DB)");
  if (kindNodeP      == NULL) KT_RVE("Database Error (No 'kind' node in cached context in DB)");
  if (createdAtNodeP == NULL) KT_RVE("Database Error (No 'createdAt' node in cached context in DB)");

  char*                 id           = idNodeP->value.s;
  char*                 url          = urlNodeP->value.s;
  double                createdAt    = createdAtNodeP->value.f;
  OrionldContextOrigin  origin       = orionldOriginFromString(originNodeP->value.s);
  OrionldContextKind    kind         = orionldKindFromString(kindNodeP->value.s);
  OrionldContext*       contextP     = orionldContextFromTree(url, origin, id, atContextP);

  if (contextP == NULL)
  {
    // Non-fatal: a cached context that can't be re-built (e.g. cyclic, or its server is down)
    // is skipped, not a startup failure. Warn so the operator knows which context was dropped.
    KT_W("unable to load cached context '%s' from DB - skipping it (%s: %s)", url, orionldState.pd.title, orionldState.pd.detail);
    return;
  }

  contextP->createdAt   = createdAt;
  contextP->usedAt      = 0;
  contextP->kind        = kind;

  if (coreContext == true)
  {
    contextP->coreContext = true;
    orionldCoreContextP   = contextP;
  }

  if (parentNodeP != NULL)
    contextP->parent = parentNodeP->value.s;
}



// -----------------------------------------------------------------------------
//
// coreContextFromFile -
//
// Context file format:
//   Line 1: The URL of the context (e.g. https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.3.jsonld)
//   Rest:   The JSON-LD content
//
// The filename is derived from the core context URL (last path component).
//
static OrionldContext* coreContextFromFile(void)
{
  if (coreContextDir[0] == 0)
    return NULL;

  const char* fileName = strrchr(coreContextUrl, '/');
  if (fileName == NULL)
    return NULL;

  ++fileName;  // skip the '/'
  char path[768];
  snprintf(path, sizeof(path), "%s/%s", coreContextDir, fileName);

  FILE* fp = fopen(path, "r");
  if (fp == NULL)
  {
    KT_T(KtCoreContext, "Core context file '%s' not found - will try to download", path);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  long fSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char* buf = (char*) malloc(fSize + 1);
  if (buf == NULL)
  {
    fclose(fp);
    return NULL;
  }

  OrionldContext* contextP = NULL;

  if (fread(buf, 1, fSize, fp) == (size_t) fSize)
  {
    buf[fSize] = 0;

    // First line is the URL - extract it and skip to the JSON
    char* json = strchr(buf, '\n');
    if (json != NULL)
    {
      *json = 0;  // null-terminate the URL line
      char* url = buf;

      // Trim trailing whitespace from URL (e.g. \r)
      int urlLen = strlen(url);
      while (urlLen > 0 && (url[urlLen - 1] == ' ' || url[urlLen - 1] == '\r' || url[urlLen - 1] == '\t'))
        url[--urlLen] = 0;

      ++json;  // skip past the newline to the JSON content

      contextP = orionldContextFromBuffer(url, OrionldContextFileCached, url, json);
      if (contextP != NULL)
        KT_V("Core context loaded from file '%s' (URL: %s)", path, url);
      else
        KT_W("Unable to parse core context from file '%s' (%s: %s)", path, orionldState.pd.title, orionldState.pd.detail);
    }
    else
      KT_W("Invalid core context file '%s' - expected URL on first line", path);
  }
  else
    KT_W("Unable to read core context file '%s'", path);

  free(buf);
  fclose(fp);

  return contextP;
}



// -----------------------------------------------------------------------------
//
// orionldContextCacheInit -
//
void orionldContextCacheInit(void)
{
  bzero(&orionldContextCacheArray, sizeof(orionldContextCacheArray));

  if (sem_init(&orionldContextCacheSem, 0, 1) == -1)
    KT_X(1, "Runtime Error (error initializing semaphore for orionld context list; %s)", strerror(errno));

  //
  // Retrieve the context cache from the database and populate the context cache in RAM
  //
  KjNode* contextArray = mongocContextCacheGet();

  //
  // Three loops:
  // 1. Core Context + cleanup
  //    - Remove all incorrect contexts from the list
  //    - If the URL of a context is the CORE-CONTEXT-URL - insert in cache and set the global pointer orionldCoreContextP
  //    - Skip all other contexts
  //    - If the core context wasn't found, then download it, create it, insert in cache, persist in DB and set the global pointer orionldCoreContextP
  //
  // 2. Key-Value contexts
  //    - Go over the context list from DB and treat only those whose context is an Object - key-values
  //    - Why?   Well, they have no dependencies, no other contexts may need downloading during this step
  //
  // 3. Array contexts
  //    - Lastly, go over all contexts of Array type
  //

  // 1. Find the Core Context
  KT_T(KtCoreContext, "Trying to find the core context (%s)", coreContextUrl);
  if (contextArray != NULL)
  {
    KjNode* contextNodeP = contextArray->value.firstChildP;
    KjNode* next;

    while (contextNodeP != NULL)
    {
      next = contextNodeP->next;

      KjNode* valueNodeP = kjLookup(contextNodeP, "value");
      KjNode* urlNodeP   = kjLookup(contextNodeP, "url");

      if (valueNodeP == NULL)
      {
        KT_E("Database Error (invalid context in orionld::contexts collection - 'value' node is missing)");
        kjChildRemove(contextArray, contextNodeP);
      }
      else if (urlNodeP == NULL)
      {
        KT_E("Database Error (invalid context in orionld::contexts collection - 'url' node is missing)");
        kjChildRemove(contextArray, contextNodeP);
      }
      else if (strcmp(urlNodeP->value.s, coreContextUrl) == 0)
      {
        kjChildRemove(contextArray, contextNodeP);
        dbContextToCache(contextNodeP, valueNodeP, true, true);
      }

      contextNodeP = next;
    }
  }

  // Still no core context? - try to load from local file
  if (orionldCoreContextP == NULL)
  {
    orionldCoreContextP = coreContextFromFile();
    if (orionldCoreContextP != NULL)
      orionldContextCachePersist(orionldCoreContextP, false);
  }

  // Still no core context? - try to download it
  if (orionldCoreContextP == NULL)
  {
    KT_T(KtCoreContext, "Still no core context - trying to download it (%s)", coreContextUrl);
    orionldCoreContextP = orionldContextFromUrl(coreContextUrl, NULL);
    if (orionldCoreContextP == NULL)
      KT_W("Unable to download the core context (%s: %s)", orionldState.pd.title, orionldState.pd.detail);
  }

  // Still no core context? - use the default core context, meant for airgapped setups
  if (orionldCoreContextP == NULL)
  {
    KT_T(KtCoreContext, "Still no core context - no network?  Getting the core context from builtin");

    //
    // The builtin core context is a string in a read-only segment.
    // The parser needs to modify it, so, the buffer needs to be copied to read/write memory.
    //
    int   bufLen = strlen(builtinCoreContext);
    char* buf    = (char*) calloc(1, bufLen + 1);

    if (buf == NULL)
      KT_X(1, "Out of memory trying to allocate %d bytes for the built-in Core Context");

    memcpy(buf, builtinCoreContext, bufLen + 1);
    orionldCoreContextP = orionldContextFromBuffer(coreContextUrl, OrionldContextBuiltinCoreContext, (char*) builtinCoreContextUrl, buf);
    free(buf);
    KT_T(KtContextCache, "Core Context at %p", orionldCoreContextP);
    if (orionldCoreContextP == NULL)
      KT_X(1, "Unable to create the core context from in-compiled default core context (%s: %s)", orionldState.pd.title, orionldState.pd.detail);
    KT_W("Falling back to Built-in Core Context (hard-coded copy of %s)", builtinCoreContextUrl);
  }

  //
  // Default User Context - can be a URL or a local file path
  //
  if (defaultUserContextUrl[0] != 0)
    defaultUserContextInit();

  if (contextArray == NULL)
    return;

  // 2. Key-Value contexts
  KjNode* contextNodeP = contextArray->value.firstChildP;
  KjNode* next;
  while (contextNodeP != NULL)
  {
    next = contextNodeP->next;

    KjNode* valueNodeP = kjLookup(contextNodeP, "value");  // If "value" not present - error in first loop

    // In this second loop, we only deal with @contexts that are Objects - key-value lists
    if (valueNodeP->type == KjObject)
    {
      kjChildRemove(contextArray, contextNodeP);
      dbContextToCache(contextNodeP, valueNodeP, true, false);
    }

    contextNodeP = next;
  }

  // Now all other contexts
  for (contextNodeP = contextArray->value.firstChildP; contextNodeP != NULL; contextNodeP = contextNodeP->next)
  {
    KjNode* valueNodeP = kjLookup(contextNodeP, "value");   // If "value" not present - error in first loop

    if (valueNodeP->type == KjArray)
      dbContextToCache(contextNodeP, valueNodeP, false, false);
    else
      KT_E("Database Error (invalid context in orionld::contexts collection - not an arry nor an object: %s", kjValueType(valueNodeP->type));
  }
}
