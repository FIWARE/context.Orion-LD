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
#include <string.h>                                              // strstr, strrchr, strlen, memcpy
#include <ctype.h>                                               // isalpha, isalnum

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjFree.h"                                        // kjFree
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/common/orionldState.h"                         // orionldState, kalloc, coreContextUrl
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/context/orionldCoreContext.h"                  // orionldCoreContextP
#include "orionld/context/orionldContextUrlGenerate.h"           // orionldContextUrlGenerate
#include "orionld/context/orionldContextSimplify.h"              // orionldContextSimplify
#include "orionld/context/orionldContextFromUrl.h"               // orionldContextFromUrl
#include "orionld/context/orionldContextFromObject.h"            // orionldContextFromObject
#include "orionld/context/orionldContextCreate.h"                // orionldContextCreate
#include "orionld/contextCache/orionldContextCacheLookup.h"      // orionldContextCacheLookup
#include "orionld/contextCache/orionldContextCacheInsert.h"      // orionldContextCacheInsert
#include "orionld/context/orionldContextFromTree.h"              // Own interface



// -----------------------------------------------------------------------------
//
// urlIsAbsolute - does the IRI reference start with a scheme?
//
// RFC 3986 § 3.1: scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
//
static bool urlIsAbsolute(const char* ref)
{
  if (isalpha(*ref) == 0)
    return false;

  for (const char* sP = &ref[1]; *sP != 0; sP++)
  {
    if (*sP == ':')
      return true;

    if ((isalnum(*sP) == 0) && (*sP != '+') && (*sP != '-') && (*sP != '.'))
      return false;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// contextRefResolve - resolve a relative @context reference against its base URL
//
// A string inside an @context array is an IRI REFERENCE, not necessarily an absolute URL, and
// JSON-LD 1.1 resolves it against the base IRI (RFC 3986 § 5). For a downloaded @context the base
// is the URL that very @context was downloaded from, so:
//
//   base:   https://a.b/x/y/compound.jsonld
//   ref:    sub.jsonld
//   result: https://a.b/x/y/sub.jsonld
//
// 'ref' is returned untouched if it is already absolute, or if there is no base to resolve against
// (an inline @context in a request payload has no URL of its own).
//
static char* contextRefResolve(const char* base, char* ref)
{
  if ((base == NULL) || (ref == NULL) || (*ref == 0) || (urlIsAbsolute(ref) == true))
    return ref;

  const char* authorityP = strstr(base, "://");

  if (authorityP == NULL)  // Not a URL we know how to take apart - leave the reference alone
    return ref;

  authorityP = &authorityP[3];

  const char* pathP = strchr(authorityP, '/');   // Start of the path inside 'base'
  const char* endP;                              // What to keep of 'base'

  if (ref[0] == '/')
  {
    if (ref[1] == '/')                           // "//host/path" - keep only "scheme:"
      endP = &strstr(base, "://")[1];
    else                                         // "/path" - keep "scheme://authority"
      endP = (pathP != NULL)? pathP : &base[strlen(base)];
  }
  else                                           // Relative path - keep everything up to the last '/'
  {
    const char* slashP = (pathP != NULL)? strrchr(pathP, '/') : NULL;

    if (slashP == NULL)                          // No path at all in base - "scheme://authority" + "/"
      endP = &base[strlen(base)];
    else
      endP = &slashP[1];

    //
    // Collapse the dot-segments of the reference (RFC 3986 § 5.2.4), the only two that occur in
    // practice: "./" is simply dropped, "../" drops one directory off the base.
    //
    while (true)
    {
      if (strncmp(ref, "./", 2) == 0)
        ref = &ref[2];
      else if (strncmp(ref, "../", 3) == 0)
      {
        if ((slashP == NULL) || (endP <= &pathP[1]))  // Cannot climb above the root
          break;

        const char* upP = endP - 1;                   // Step onto the trailing '/' ...

        while ((upP > pathP) && (upP[-1] != '/'))     // ... and back to the one before it
          upP -= 1;

        endP = upP;
        ref  = &ref[3];
      }
      else
        break;
    }
  }

  int   baseLen = endP - base;
  int   refLen  = strlen(ref);
  char* urlP    = (char*) kaAlloc(&kalloc, baseLen + refLen + 2);

  if (urlP == NULL)
    return ref;

  memcpy(urlP, base, baseLen);

  if ((baseLen > 0) && (urlP[baseLen - 1] != '/') && (ref[0] != '/'))
    urlP[baseLen++] = '/';

  memcpy(&urlP[baseLen], ref, refLen);
  urlP[baseLen + refLen] = 0;

  return urlP;
}



// -----------------------------------------------------------------------------
//
// orionldContextFromTree -
//
OrionldContext* orionldContextFromTree(char* url, OrionldContextOrigin origin, char* id, KjNode* contextTreeP)
{
  int itemsInArray;

  if (contextTreeP->type == KjArray)
  {
    contextTreeP = orionldContextSimplify(contextTreeP, &itemsInArray);
    if ((contextTreeP == NULL) || (contextTreeP->value.firstChildP == NULL))
    {
      // Nothing left in  the array - only Core Context was there but has been removed?
      orionldState.pd.status = 200;

      return orionldCoreContextP;
    }

    if ((itemsInArray == 1) && (contextTreeP->value.firstChildP->type == KjString))
      orionldState.link = contextTreeP->value.firstChildP->value.s;

    //
    // Need to clone the array and add it to the context cache
    //
    bool            arrayToCache = (url != NULL);
    OrionldContext* contextP     = orionldContextCreate(url, origin, id, contextTreeP, false);

    //
    // Once created, the parameter 'url' needs to be "invalidated", as it's already been used - to avoid to use the same URL for children of the context
    // It is kept as 'baseUrl' though - not as an identity for the children, but as the base that RELATIVE references among them resolve against
    //
    char* baseUrl = url;

    url = NULL;
    id  = NULL;

    if (contextP == NULL)
      KT_RE(NULL, "Internal Error (unable to create context)");


    contextP->context.array.items     = itemsInArray;
    contextP->context.array.vector    = (OrionldContext**) kaAlloc(&kalloc, itemsInArray * sizeof(OrionldContext*));

    int ix = 0;
    for (KjNode* ctxItemP = contextTreeP->value.firstChildP; ctxItemP != NULL; ctxItemP = ctxItemP->next)
    {
      OrionldContext* cachedContextP = NULL;
      char*           itemUrl        = NULL;

      if (ctxItemP->type == KjString)
      {
        //
        // Resolved BEFORE the cache lookup, so that it is the absolute form that is looked up and
        // later cached - the very same relative reference under two different base URLs points at
        // two different @contexts
        //
        itemUrl = contextRefResolve(baseUrl, ctxItemP->value.s);

        //
        // The resolved form replaces the reference in the tree, so that everything downstream - the
        // recursive call below and the download it ends up doing - sees the absolute URL.
        // Only ever a change when there IS a base, i.e. for a @context of our own that was downloaded
        // or is hosted here; an inline @context in a request payload has no base and is left alone.
        //
        ctxItemP->value.s = itemUrl;
        cachedContextP    = orionldContextCacheLookup(itemUrl);
      }
      else if ((ctxItemP->type != KjObject) && (ctxItemP->type != KjArray))
      {
        orionldError(OrionldBadRequestData, "Invalid @context - invalid type for @context array item", kjValueType(ctxItemP->type), 400);

        if (contextP->url != NULL)  // Only contexts with a URL are "kj-cloned"
          kjFree(contextP->tree);

        return NULL;
      }

      if (cachedContextP == NULL)
      {
        if (ctxItemP->type == KjString)
        {
          url = itemUrl;               // The reference, resolved against baseUrl if it was relative
          id  = (char*) "downloaded";  // FIXME: perhaps NULL is a better value ...
        }
        else if (origin == OrionldContextUserCreated)
          url = orionldContextUrlGenerate(&id);

        contextP->context.array.vector[ix] = orionldContextFromTree(url, origin, id, ctxItemP);
        if (contextP->context.array.vector[ix] != NULL)
          contextP->context.array.vector[ix]->parent = contextP->id;
        else
        {
          if (contextP->url != NULL)
            kjFree(contextP->tree);
          // Non-fatal (e.g. a rejected cyclic @context) - warn and bail; don't fail startup
          KT_W("unable to resolve context '%s'", url);
          return NULL;
        }
      }
      else
        contextP->context.array.vector[ix] = cachedContextP;

      // Again, url and id needs to be NULLed out for the next loop
      url = NULL;
      id  = NULL;

      ++ix;
    }

    if (arrayToCache == true)
      orionldContextCacheInsert(contextP);

    return contextP;
  }
  else if (contextTreeP->type == KjString)
  {
    if (url != NULL)
    {
      OrionldContext* contextP = orionldContextCacheLookup(url);

      if (contextP == NULL)
      {
        contextP = orionldContextCreate(url, origin, id, contextTreeP, false);

        contextP->context.array.items     = 1;
        contextP->context.array.vector    = (OrionldContext**) kaAlloc(&kalloc, 1 * sizeof(OrionldContext*));
        //
        // NOT 'url' - 'url' is the URL of THIS @context, while contextTreeP->value.s is the reference
        // it points to, and for a @context that is a plain string those two are different things
        //
        contextP->context.array.vector[0] = orionldContextFromUrl(contextTreeP->value.s, NULL);

        if (contextP->context.array.vector[0] == NULL)
        {
          KT_W("Context could not be resolved via orionldContextFromUrl");
          return NULL;
        }
      }

      if (contextP != NULL)
        contextP->origin = origin;

      return contextP;
    }
    else
    {
      OrionldContext* contextP;
      contextP = orionldContextFromUrl(contextTreeP->value.s, NULL);
      if (contextP != NULL)
      {
        if (contextP->origin != OrionldContextDownloaded)
          contextP->origin = origin;
      }

      return contextP;
    }
  }
  else if (contextTreeP->type == KjObject)
  {
    OrionldContext* contextP = orionldContextFromObject(url, origin, id, contextTreeP);

    if (contextP)
      contextP->origin = origin;

    return contextP;
  }

  //
  // None of the above. Error
  //
  orionldError(OrionldBadRequestData, "Invalid type for item in @context array", kjValueType(contextTreeP->type), 400);
  return NULL;
}
