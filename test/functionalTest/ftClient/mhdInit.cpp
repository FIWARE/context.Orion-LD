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
}

#include "mhd/mhdStart.h"                                   // mhdStart - initialize MHD and start receiving REST requests
#include "mhd/mhdConnectionPayloadRead.h"                   // mhdConnectionPayloadRead

#include "ftClient/mhdRequestInit.h"                        // mhdRequestInit
#include "ftClient/mhdRequestTreat.h"                       // mhdRequestTreat
#include "ftClient/mhdRequestEnded.h"                       // mhdRequestEnded
#include "ftClient/mhdInit.h"                               // Own interface



// -----------------------------------------------------------------------------
//
// mhdInit -
//
void mhdInit(unsigned short port)
{
  if (mhdStart(port, 4, mhdRequestInit, mhdConnectionPayloadRead, mhdRequestTreat, mhdRequestEnded) == false)
    KT_X(1, "Unable to start REST interface on port %d", port);

  KT_V("Serving requests on port %d", port);
}
