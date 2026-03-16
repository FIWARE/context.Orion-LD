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
extern "C"
{
#include "ktrace/ktGlobals.h"                  // ktTraceLevels
}

#include "common/globals.h"                    // transactionIdSet



// ----------------------------------------------------------------------------
//
// ktTraceIsSet - check if a trace level is set
//
#define ktTraceIsSet(level) ((ktTraceLevels[(level) / 32] & (1 << ((level) % 32))) != 0)



// ----------------------------------------------------------------------------
//
// lmTransaction* stubs - ktrace doesn't have transaction tracking, so these are no-ops
// lmTransactionStart calls transactionIdSet() to increment the transaction counter
//
#define lmTransactionStart(...) transactionIdSet()
#define lmTransactionEnd()
#define lmTransactionSetFrom(x)
#define lmTransactionSetService(x)
#define lmTransactionSetSubservice(x)
#define lmTransactionReset()
#define lmTraceLevelSet(level, onOff)

// Old trace level constants - stubs for compatibility
#define LmtLegacy 0



// ----------------------------------------------------------------------------
//
// Trace Levels -
//
typedef enum OrionldTraceLevels
{
  StMhdInit                 = 100,

  StRequest                 = 200,
  KtRequest                 = 200,
  KtResponse                = 201,
  StRequestHeaders          = 202,
  StRequestParams           = 203,
  KtSR                      = 210,

  // URL Params
  KtUrlParam                = 250,
  KtCsf                     = 251,
  KtCount                   = 252,
  KtPick                    = 253,
  KtFormat                  = 254,
  KtSysAttrs                = 255,
  KtQ                       = 256,
  KtQ2                      = 257,
  KtQ3                      = 258,
  KtOptions                 = 259,

  // HTTP Headers
  KtHttpHeader              = 280,
  KtLinkHeader              = 281,

  // JSON-LD
  KtArrayReduction          = 301,

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
  KtWatchedAttributes       = 802,

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
  KtSubCacheMatch           = 903,
  KtSubCacheDebug           = 904,
  KtSubCacheFlush           = 905,

  // Alterations
  KtAlt                     = 1000,

  // Notifications
  KtNotification            = 1100,
  KtNotificationStats       = 1101,
  KtNotificationHeaders     = 1102,
  KtNotificationBody        = 1103,
  KtNotificationSend        = 1104,
  KtNotificationMsg         = 1105,
  KtShowChanges             = 1106,

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
  KtDistOpMerge             = 1210,
  KtDistOp207               = 1211,
  KtDistOpLoop              = 1212,
  KtDistOpResponseBuf       = 1213,
  KtDistOpSubMatch          = 1214,

  // Entity Maps
  KtEntityMap               = 1300,
  KtEntityMapRetrieve       = 1301,
  KtEntityMapDetail         = 1302,

  // GeoJSON
  KtGeoJSON                 = 1350,

  // TRoE
  KtTroe                    = 1400,
  KtTroeFilter              = 1401,
  KtPgPool                  = 1402,
  KtSql                     = 1403,
  KtPostgres                = 1404,

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
  KtContextCachePersist     = 1752,

  // DB Model
  KtDbModel                 = 1800,
  KtAttrNames               = 1801,
  KtDbModel2                = 1802,

  // API Model
  KtApiModel                = 1850,

  // Dataset ID
  KtDatasetId               = 1900,

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
  StWs                      = 3020,
  KtWsTest                  = 3021,

  // Legacy (old mongo C++ driver code, old parsers, etc.)
  KtLegacy                  = 3100,
  KtLegacySubMatch          = 3101,
  KtLegacySubCacheRefresh   = 3102,

  // Misc
  KtLeak                    = 3110,
  KtToDo                    = 3111,
  KtDateTime                = 3112,
  KtUriEncode               = 3113,
  KtSemaphore               = 3114,
  KtKjParse                 = 3115,
  KtTenants                 = 3116,
  KtRegex                   = 3117,
  KtMimeType                = 3118,
  KtBug                     = 3119,
  KtPatchEntity             = 3120,
  KtPatchEntity2            = 3121,
  KtPerformance             = 3122,
  KtPrometheus              = 3123,

  // Kafka
  StKafka                   = 2100,
  StKafkaDetail             = 2101,

  // FT Client
  StDump                    = 3151,
  StDdsDump                 = 3152
} OrionldTraceLevels;

#endif  // SRC_LIB_ORIONLD_COMMON_TRACELEVELS_H_
