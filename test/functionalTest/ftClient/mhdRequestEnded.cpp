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
#include <microhttpd.h>                                          // MHD

extern "C"
{
#include "ktrace/kTrace.h"                                  // trace messages - ktrace library
#include "kalloc/kaBufferReset.h"                           // kaBufferReset
#include "kjson/kjFree.h"                                   // kjFree
}

#include "common/orionldState.h"                            // orionldState
#include "common/traceLevels.h"                             // Trace levels for ktrace



// -----------------------------------------------------------------------------
//
// mhdRequestEnded -
//
void mhdRequestEnded
(
  void*                       cls,
  MHD_Connection*             connection,
  void**                      con_cls,
  MHD_RequestTerminationCode  toe
)
{
  KT_T(StRequest, "Request ended");

  kaBufferReset(&orionldState.kalloc, true);

  if ((orionldState.responseTree != NULL) && (orionldState.kjsonP == NULL))
    kjFree(orionldState.responseTree);

  *con_cls = NULL;

  // payloadBodySize  = 0;
  // contentLength    = 0;
  // payloadBody      = NULL;
  // payloadTree      = NULL;
  // verb             = HTTP_NOVERB;
  // urlPath          = NULL;
  // httpHeaders      = NULL;
  // uriParams        = NULL;

  // Reset kjson/kalloc
  // kaBufferReset(&ftKalloc, KFALSE);
  // free(ftKjsonP);
}
