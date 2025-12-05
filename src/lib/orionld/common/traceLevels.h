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
  KtSR                      = 210,

  // URL Params
  KtUrlParam                = 300,
  KtCsf                     = 301,
  KtCount                   = 302,
  KtPick                    = 303,
  KtFormat                  = 304,
  KtSysAttrs                = 305,
  KtQ                       = 306,

  StLinked                  = 400,
  StLinkedInline            = 401,
  StLinkedInline2           = 402,

  // Mongoc driver
  StMongoc                  = 500,

  // Mongo C++ Legacy driver
  StMongo                   = 600,
  StMongoPool               = 601,

  // Registration Cache
  KtRegCache                = 700,

  // Subscriptions
  KtSubs                    = 800,
  KtPernot                  = 801,
  KtSubordinate             = 802,
  KtShowChanges             = 803,

  // Subscription Cache
  KtSubCache                = 900,
  KtSubCacheSync            = 901,
  KtSubCacheStats           = 902,

  // Alterations
  KtAlt                     = 1000,

  // Notifications
  KtNotification            = 1100,
  KtNotificationStats       = 1101,

  // Distributed Operations
  KtDistOp                  = 1200,
  KtDistOpRequest           = 1201,
  KtDistOpList              = 1202,
  KtDistOpResponseDetail    = 1203,
  KtDistOpAttributes        = 1204,
  KtDistOpAttrRemove        = 1205,

  // Entity Maps
  KtEntityMap               = 1300,

  // TRoE
  KtTroe                    = 1400,

  // DDS
  StDds                     = 2001,
  StDdsPublish              = 2002,
  StDdsNotification         = 2003,
  StDdsLibInfo              = 2004,
  StDdsLibDebug             = 2005,
  StDdsConfig               = 2006,
  StDdsTypes                = 2007,
  StDdsTypeCache            = 2008,
  StDdsPrePopulate          = 2009,
  StDdsSrCalls              = 1010,
  StDdsService              = 2020,
  StDdsServiceList          = 2021,
  StDdsServicePrepopulate   = 2022,
  StDdsAction               = 2030,

  // FT Client
  StDump                    = 3001,
  StDdsDump                 = 3002
} OrionldTraceLevels;

#endif  // SRC_LIB_ORIONLD_COMMON_TRACELEVELS_H_
