#ifndef SRC_LIB_ORIONLD_COMMON_TRACELEVELS_H_
#define SRC_LIB_ORIONLD_COMMON_TRACELEVELS_H_

/*
*
* Copyright 2022 FIWARE Foundation e.V.
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



// ----------------------------------------------------------------------------
//
// Trace Levels -
//
typedef enum OrionldTraceLevels
{
  StMhdInit                 = 100,

  StRequest                 = 200,
  StRequestHeaders          = 201,
  StRequestParams           = 202,
  StSR                      = 210,

  StLinked                  = 300,
  StLinkedInline            = 301,
  StLinkedInline2           = 302,

  StMongoc                  = 400,

  StDds                     = 1001,
  StDdsPublish              = 1002,
  StDdsNotification         = 1003,
  StDdsLibInfo              = 1004,
  StDdsLibDebug             = 1005,
  StDdsConfig               = 1006,
  StDdsTypes                = 1007,
  StDdsTypeCache            = 1008,
  StDdsPrePopulate          = 1009,
  StDdsSrCalls              = 1010,
  StDdsService              = 1020,
  StDdsServiceList          = 1021,
  StDdsServicePrepopulate   = 1022,

  StDdsAction               = 1030,

  StDump                    = 2001,
  StDdsDump                 = 2002
} OrionldTraceLevels;

#endif  // SRC_LIB_ORIONLD_COMMON_TRACELEVELS_H_
