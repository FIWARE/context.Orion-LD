/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include <string.h>                                            // strlen, strncpy, memcpy
#include <stdlib.h>                                            // malloc, realloc

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/types/PgAppendBuffer.h"                      // PgAppendBuffer
#include "orionld/troe/pgAppend.h"                             // Own interface



// ----------------------------------------------------------------------------
//
// pgAppend -
//
void pgAppend(PgAppendBuffer* pgBufP, const char* tail, int tailLen)
{
  if (tail == NULL)
    return;

  if (tailLen <= 0)
    tailLen = strlen(tail);

  // realloc necessary?  (+1 for null terminator)
  if (pgBufP->currentIx + tailLen + 1 >= pgBufP->bufSize)
  {
    char* oldBuffer = pgBufP->buf;

    // Grow by at least the needed amount, using doubling strategy for large buffers
    int needed = pgBufP->currentIx + tailLen + 1;

    if (needed < 16 * 1024)
    {
      // For small buffers: round up to next 4KB boundary
      pgBufP->bufSize = (needed + 4095) & ~4095;
    }
    else
    {
      // For large buffers: double the size or use needed + 25%, whichever is larger
      int doubled     = pgBufP->bufSize * 2;
      int withMargin  = needed + (needed / 4);

      pgBufP->bufSize = (doubled > withMargin) ? doubled : withMargin;
    }

    if (pgBufP->bufSize < 16 * 1024)  // Use kaAlloc for smaller buffers
    {
      pgBufP->buf = kaAlloc(&orionldState.kalloc, pgBufP->bufSize);
      if (pgBufP->buf == NULL)
      {
        // kaAlloc failed, fall through to malloc
        pgBufP->buf = (char*) malloc(pgBufP->bufSize);
        if (pgBufP->buf == NULL)
        {
          KT_E("pgAppend: out of memory allocating %d bytes", pgBufP->bufSize);
          pgBufP->buf = oldBuffer;  // Restore old buffer to avoid NULL dereference
          return;
        }
        pgBufP->allocated = true;
      }
      memcpy(pgBufP->buf, oldBuffer, pgBufP->currentIx);
      pgBufP->buf[pgBufP->currentIx] = 0;
    }
    else
    {
      char* newBuf;

      if (pgBufP->allocated == true)
      {
        newBuf = (char*) realloc(pgBufP->buf, pgBufP->bufSize);
        if (newBuf == NULL)
        {
          KT_E("pgAppend: realloc failed for %d bytes", pgBufP->bufSize);
          // Keep old buffer intact, skip this append
          return;
        }
        pgBufP->buf = newBuf;
      }
      else
      {
        newBuf = (char*) malloc(pgBufP->bufSize);
        if (newBuf == NULL)
        {
          KT_E("pgAppend: malloc failed for %d bytes", pgBufP->bufSize);
          return;
        }
        memcpy(newBuf, oldBuffer, pgBufP->currentIx);
        newBuf[pgBufP->currentIx] = 0;
        pgBufP->buf = newBuf;
      }

      pgBufP->allocated = true;
    }
  }

  memcpy(&pgBufP->buf[pgBufP->currentIx], tail, tailLen);
  pgBufP->currentIx += tailLen;
  pgBufP->buf[pgBufP->currentIx] = 0;
}
