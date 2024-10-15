#ifndef SRC_LIB_ORIONLD_MHD_MHDSTART_H_
#define SRC_LIB_ORIONLD_MHD_MHDSTART_H_

/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include <microhttpd.h>                                          // MHD_Result, MHD_Connection



// -----------------------------------------------------------------------------
//
// MhdRequestInit  - function for type   I of MHD Request callbacks (read headers and URI params)
// MhdRequestBody  - function for type  II of MHD Request callbacks (read payload body)
// MhdRequestTreat - function for type III of MHD Request callbacks (all done - treat the request)
//
typedef MHD_Result (*MhdRequestInit)(MHD_Connection* connection, const char* url, const char* method, const char* version, void** con_cls);
typedef MHD_Result (*MhdRequestBody)(size_t* upload_data_size, const char* upload_data);
typedef char*      (*MhdRequestTreat)(int* statusCodeP);
typedef void       (*MhdRequestEnd)(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);


// -----------------------------------------------------------------------------
//
// mhdStart -
//
extern bool mhdStart
(
  unsigned short   ldPort,
  int              ipVersion,
  MhdRequestInit   initF,
  MhdRequestBody   bodyF,
  MhdRequestTreat  treatF,
  MhdRequestEnd    endF
);

#endif  // SRC_LIB_ORIONLD_MHD_MHDSTART_H_
