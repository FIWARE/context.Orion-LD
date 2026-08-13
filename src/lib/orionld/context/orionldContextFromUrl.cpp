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
#include <unistd.h>                                              // usleep
#include <semaphore.h>                                           // sem_init, sem_wait, sem_post
#include <pthread.h>                                             // pthread_self, pthread_equal, pthread_t

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/types/OrionldContext.h"                        // OrionldContext
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/context/orionldContextFromBuffer.h"            // orionldContextFromBuffer
#include "orionld/contextCache/orionldContextCacheLookup.h"      // orionldContextCacheLookup
#include "orionld/context/orionldContextDownload.h"              // orionldContextDownload
#include "orionld/contextCache/orionldContextCachePersist.h"     // orionldContextCachePersist
#include "orionld/context/orionldContextFromUrl.h"               // Own interface



// -----------------------------------------------------------------------------
//
// StringListItem -
//
typedef struct StringListItem
{
  char                    name[256];
  pthread_t               owner;   // The thread that is downloading this URL - to detect cyclic @contexts (same-thread re-entry)
  struct StringListItem*  next;
} StringListItem;

static sem_t            contextDownloadListSem;
static StringListItem*  contextDownloadList = NULL;



// -----------------------------------------------------------------------------
//
// contextDownloadListInit - initialize the 'context download list'
//
void contextDownloadListInit(void)
{
  sem_init(&contextDownloadListSem, 0, 1);  // 0: shared between threads of the same process. 1: free to be taken
  contextDownloadList = NULL;
}



// -----------------------------------------------------------------------------
//
// contextDownloadListLookup - lookup a URL in the list and report if found or not
//
bool contextDownloadListLookup(const char* url)
{
  StringListItem* itemP = contextDownloadList;

  KT_T(KtContextDownload, "Looking for context URL '%s'", url);
  while (itemP != NULL)
  {
    KT_T(KtContextDownload, "Comparing existing '%s' to wanted '%s'", itemP->name, url);
    if (strcmp(itemP->name, url) == 0)
    {
      KT_T(KtContextDownload, "Found a match: '%s'", url);
      return true;
    }

    itemP = itemP->next;
  }

  KT_T(KtContextDownload, "Found no match for '%s'", url);
  return false;
}



// -----------------------------------------------------------------------------
//
// contextDownloadListOwnedByMe - is 'url' being downloaded by the CURRENT thread?
//
// 'contextCacheWait' exists to let one thread wait for ANOTHER thread that is already
// downloading the same @context (so we don't download it twice). But if the URL was put
// in the download list by THIS very thread, then we have recursed back into a context that
// references itself (directly or via a chain) - a cyclic @context. Waiting is pointless:
// the download that would satisfy the wait is the frame that is now blocked here, so the
// wait burns its full timeout and then fails anyway. This lets the caller detect that case.
//
static bool contextDownloadListOwnedByMe(const char* url)
{
  pthread_t me = pthread_self();

  for (StringListItem* itemP = contextDownloadList; itemP != NULL; itemP = itemP->next)
  {
    if ((strcmp(itemP->name, url) == 0) && (pthread_equal(itemP->owner, me) != 0))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// contextDownloadListDebug -
//
static void contextDownloadListDebug(const char* what)
{
  KT_T(KtContextDownload, "contextDownloadList (%s)", what);
  KT_T(KtContextDownload, "----------------------------------------------------");

  for (StringListItem* iterP = contextDownloadList; iterP != NULL; iterP = iterP->next)
  {
    KT_T(KtContextDownload, "  o %s", iterP->name);
  }

  KT_T(KtContextDownload, "----------------------------------------------------");
}



// -----------------------------------------------------------------------------
//
// contextDownloadListAdd - add a URL to the list
//
void contextDownloadListAdd(const char* url)
{
  StringListItem* itemP = (StringListItem*) malloc(sizeof(StringListItem));

  KT_T(KtContextDownload, "Adding '%s' to contextDownloadList", url);
  strncpy(itemP->name, url, sizeof(itemP->name) - 1);
  itemP->owner = pthread_self();
  itemP->next = contextDownloadList;
  contextDownloadList = itemP;
  contextDownloadListDebug("after item added");
}



// -----------------------------------------------------------------------------
//
// contextDownloadListRemove - remove a URL from the list
//
void contextDownloadListRemove(const char* url)
{
  StringListItem* iterP = contextDownloadList;
  StringListItem* prevP = NULL;
  StringListItem* itemP = NULL;

  KT_T(KtContextDownload, "Removing '%s' from contextDownloadList", url);

  while (iterP != NULL)
  {
    if (strcmp(iterP->name, url) == 0)
    {
      itemP = iterP;
      break;
    }

    prevP = iterP;
    iterP = iterP->next;
  }

  if (itemP == NULL)  // Not found!
  {
    KT_T(KtContextDownload, "Cannot find '%s' in contextDownloadList", url);
    return;
  }

  if (prevP == NULL)  // Found as the first item of the list
  {
    KT_T(KtContextDownload, "Removing '%s' as first item in contextDownloadList", url);
    contextDownloadList = itemP->next;
    free(itemP);
  }
  else if (itemP->next == NULL)  // Found as the last item of the list
  {
    KT_T(KtContextDownload, "Removing '%s' as last item in contextDownloadList", url);
    prevP->next = NULL;
    free(itemP);
  }
  else  // Found in the middle of the list
  {
    KT_T(KtContextDownload, "Removing '%s' as middle item in contextDownloadList", url);
    prevP->next = itemP->next;
    free(itemP);
  }

  contextDownloadListDebug("after item removal");
}



// -----------------------------------------------------------------------------
//
// contextDownloadListRelease - release all items in the 'context download cache'
//
// The cache is self-cleaning and this function isn't really necessary - except perhaps
// if the broker is killed while serving requests, e.g. while running tests.
//
// This function is ONLY called from the main exit-function, to avoid leaks for valgrind tests.
//
void contextDownloadListRelease(void)
{
  StringListItem* iterP = contextDownloadList;

  while (iterP != NULL)
  {
    StringListItem* current = iterP;
    iterP = iterP->next;
    free(current);
  }
}



// -----------------------------------------------------------------------------
//
// contextCacheWait -
//
static OrionldContext* contextCacheWait(char* url)
{
  int             sleepTime = 0;
  OrionldContext* contextP;

  KT_T(KtContextDownload, "Awaiting a context download by other (URL: %s)", url);

  while (sleepTime < 3000000)  // 3 secs - 3 million microsecs ... CLI param?
  {
    usleep(20000);  // sleep 20 millisecs ... CLI param?
    KT_T(KtContextDownload, "Awaiting context download: looking up context '%s'", url);
    contextP = orionldContextCacheLookup(url);
    if (contextP != NULL)
    {
      KT_T(KtContextDownload, "Got it! (%s)", url);
      return contextP;
    }
    KT_T(KtContextDownload, "Still not there (%s)", url);
    sleepTime += 20000;
  }
  KT_T(KtContextDownload, "Timeout during download of an @context (%s)", url);

  // The wait timed out
  orionldError(OrionldInternalError, "Timeout during download of an @context", url, 504);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// contextCacheWaitOrCycleBreak -
//
// Called when 'url' is already in the download list. If ANOTHER thread is downloading it,
// wait for that download to finish (contextCacheWait). If THIS thread put it there, we have
// recursed back into our own in-progress download = a cyclic @context; waiting would just
// burn the full timeout and fail, so break the cycle immediately with a clear error.
//
static OrionldContext* contextCacheWaitOrCycleBreak(char* url)
{
  if (contextDownloadListOwnedByMe(url) == true)
  {
    // Cyclic @context - non-fatal: we reject this @context and carry on (the broker still
    // starts / the request still gets an error response from the caller). So it's a WARNING,
    // not an error - emitting an 'E:' here would make orionldStart consider startup failed.
    KT_W("Cyclic @context detected - '%s' references itself (directly or via a chain) - rejecting it", url);

    // Set the problem details DIRECTLY (orionldError() would log at 'E:', which orionldStart
    // treats as a fatal startup error). This gives a request-time cycle a proper 400 response
    // and, with status >= 300, suppresses the generic "Unable to download context" fallback.
    orionldState.pd.type   = OrionldBadRequestData;
    orionldState.pd.title  = (char*) "Cyclic @context";
    orionldState.pd.detail = url;
    orionldState.pd.status = 400;

    return NULL;
  }

  return contextCacheWait(url);
}



// -----------------------------------------------------------------------------
//
// orionldContextFromUrl -
//
OrionldContext* orionldContextFromUrl(char* url, char* id)
{
  KT_T(KtContextDownload, "Possibly downloading a context URL: '%s'", url);

  OrionldContext* contextP = orionldContextCacheLookup(url);

  if (contextP != NULL)
  {
    contextP->usedAt   = orionldState.requestTime;

    contextP->lookups += 1;
    KT_T(KtContextCacheStats, "Context '%s': %d lookups", url, contextP->lookups);

    KT_T(KtContextDownload, "Found already downloaded URL '%s'", url);
    return contextP;
  }

  //
  // ⭐ AN HA APPLY GETS ZERO HOPS. It resolves the one item the event named, and
  // follows nothing.
  //
  // The reason it can afford to is the shape of the channel itself: anything this
  // @context references is an item somebody else has downloaded and PERSISTED, and
  // that persist raises an event of its own. So a missing reference is not
  // something to go and fetch - it is something that arrives, by the same road,
  // announced separately. Chasing it here would mean an HTTP download inside the
  // change-stream thread and then a row written back that is already in the
  // database, which is the one thing an apply must never do.
  //
  // Hence a warning and not an error: a miss is an ordering observation, not a
  // failure. It is rare - the creating instance persists the members of an array
  // @context before the array that references them, so the events arrive in that
  // order - and it costs nothing when it happens, because the @context is in the
  // database and the next request that needs it resolves it from there.
  //
  if (orionldState.haApply == true)
  {
    KT_W("HA: @context '%s' is not cached and an apply takes no hops - it will arrive as an event of its own", url);
    return NULL;
  }

  //
  // Make sure the context isn't already being downloaded
  //
  // Three possibilities:
  // CASE 1. No one is trying to download the context, so:
  //         - mark the URL to be downloading - for the next to know
  //         - download it and add it to the context cache
  //         - remove the mark from step 1
  // CASE 2. No one was trying to download the context, so I tried to take the semaphore.
  //         However, another thread got the semaphore before me and started the download
  //         In this case, I will NOT try to download (somebody else is already doing that).
  //         Instead, I will wait for that download to finish and then lookup the context from the cache
  //
  // CASE 3. Someone was actually downloading the context when I wanted to do the same.
  //         Just like step 2 - I wait for the download to complete and then lookup the context from the cache.
  //
  bool urlDownloading = contextDownloadListLookup(url);
  if (urlDownloading == false)
  {
    //
    // Not there, so, we'll download it
    // First take the 'download semaphore'
    //
    KT_T(KtContextDownload, "The context '%s' is not downloading, getting the downloadList semaphore", url);
    sem_wait(&contextDownloadListSem);
    KT_T(KtContextDownload, "Got the downloadList semaphore for '%s'", url);

    //
    // OK - got the semaphore - but, did I have to wait?
    // I must look the context up again, to be sure (in the 'contextDownloadList')
    //
    // If the URL is in 'contextDownloadList' then somebody else took the semaphore before me
    // and started downloading.
    //
    KT_T(KtContextDownload, "Looking up '%s' again, in case I got the semaphore late", url);
    urlDownloading = contextDownloadListLookup(url);
    if (urlDownloading == false)
    {
      KT_T(KtContextDownload, "The context '%s' is not downloading by other - will be downloaded here", url);
      contextDownloadListAdd(url);  // CASE 1: Mark the URL as being downloading
    }

    KT_T(KtContextDownload, "Giving back the downloadList semaphore for '%s'", url);
    sem_post(&contextDownloadListSem);

    if (urlDownloading == true)  // If somebody has taken the semaphore before me and is downloading the context - I'll have to wait
    {
      KT_T(KtContextDownload, "The context '%s' is downloading by other - I wait until it's done", url);
      return contextCacheWaitOrCycleBreak(url);  // CASE 2 - another thread is downloading the context (or WE are - a cycle)
    }

    // CASE 1 - the context will be downloaded
  }
  else
  {
    KT_T(KtContextDownload, "The context '%s' is downloading by other - I wait until it's done", url);
    return contextCacheWaitOrCycleBreak(url);  // CASE 3 - another thread is downloading the context (or WE are - a cycle)
  }

  KT_T(KtContextDownload, "Downloading the context '%s' and adding it to the context cache", url);
  char* buffer = orionldContextDownload(url);  // orionldContextDownload fills in ProblemDetails

  if (buffer != NULL)  // All OK
  {
    KT_T(KtCoreContext, "Downloaded the context '%s'", url);
    contextP = orionldContextFromBuffer(url, OrionldContextDownloaded, id, buffer);
    if (contextP == NULL)
    {
      // Non-fatal: the @context could not be built (e.g. a cyclic reference rejected deeper
      // down). Warn instead of error so startup isn't considered failed; request handling
      // still turns the NULL return into an error response upstream.
      KT_W("Context Warning (%s: %s)", orionldState.pd.title, orionldState.pd.detail);
      if (orionldState.pd.status < 300)  // Error not filled in
        orionldError(OrionldLdContextNotAvailable, "Unable to download context", url, 504);
    }
  }
  else
    KT_W("Context Warning (%s: %s)", orionldState.pd.title, orionldState.pd.detail);

  if (contextP != NULL)
  {
    contextP->origin    = OrionldContextDownloaded;
    contextP->usedAt    = orionldState.requestTime;

    orionldContextCachePersist(contextP, false);
  }

  // Remove the 'url' from the contextDownloadList and persist it to DB
  sem_wait(&contextDownloadListSem);
  contextDownloadListRemove(url);
  sem_post(&contextDownloadListSem);

  return contextP;
}
