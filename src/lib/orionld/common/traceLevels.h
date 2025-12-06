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
  KtRequest                 = 200,
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
  KtQ2                      = 307,
  KtQ3                      = 308,
  KtOptions                 = 309,

  // HTTP Headers
  KtHttpHeader              = 350,
  KtLinkHeader              = 351,

  // Linked Entities
  StLinked                  = 400,
  StLinkedInline            = 401,
  StLinkedInline2           = 402,

  // Mongoc driver
  StMongoc                  = 500,
  KtMongoc                  = 500,

  // Mongo C++ Legacy driver
  StMongo                   = 600,
  StMongoPool               = 601,

  // Registration Cache
  KtRegCache                = 700,

  // Registration Match
  KtRegMatch                = 750,

  // Subscriptions
  KtSubs                    = 800,
  KtSubordinate             = 801,
  KtShowChanges             = 802,

  // Periodic Subscriptions
  KtPernot                  = 850,
  KtPernotLoop              = 851,
  KtPernotLoopTimes         = 852,
  KtPernotFlush             = 853,
  KtPernotQuery             = 854,

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
  KtDistOpResponse          = 1202,
  KtDistOpResponseDetail    = 1203,
  KtDistOpList              = 1204,
  KtDistOpAttributes        = 1205,
  KtDistOpAttrRemove        = 1206,
  KtDistOpRequestHeaders    = 1207,
  KtDistOpResponseHeaders   = 1208,
  KtDistOpRequestParams     = 1209,

  // Entity Maps
  KtEntityMap               = 1300,

  // TRoE
  KtTroe                    = 1400,

  // Config File
  KtConfig                  = 1500,

  // Socket Service
  KtSocketService           = 1600,

  // Context
  KtContext                 = 1700,
  KtContextInBody           = 1701,
  KtExpand                  = 1702,
  KtCompact                 = 1703,
  KtCoreContext             = 1704,
  KtUserContext             = 1705,
  KtDefaultUserContext      = 1706,
  KtContextDownload         = 1707,
  KtContextItem             = 1708,
  KtContextTree             = 1709,

  // Context Cache
  KtContextCache            = 1750,
  KtContextCacheStats       = 1751,

  // DB Model
  KtDbModel                 = 1800,

  // API Model
  KtApiModel                = 1850,

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

  // 3rd Party
  KtCurl                    = 3001,
  KtMqtt                    = 3010,

  // Misc
  KtLeak                    = 4000,

  // FT Client
  StDump                    = 5001,
  StDdsDump                 = 5002
} OrionldTraceLevels;

#endif  // SRC_LIB_ORIONLD_COMMON_TRACELEVELS_H_
