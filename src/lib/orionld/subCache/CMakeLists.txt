# Copyright 2026 FIWARE Foundation e.V.
#
# This file is part of Orion-LD Context Broker.
#
# Orion-LD Context Broker is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# Orion-LD Context Broker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
# General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
#
# For those usages not covered by this license please contact with
# orionld at fiware dot org
#
# Author: Ken Zangelin
CMAKE_MINIMUM_REQUIRED(VERSION 3.5)

SET (SOURCES
    subCacheCreate.cpp
    subCacheItemAdd.cpp
    subCacheItemLookup.cpp
    subCacheItemUpdate.cpp
    subCacheItemRemove.cpp
    subCacheItemRelease.cpp
    subCacheItemStatusSet.cpp
    subCacheItemCountersFlush.cpp
    subCachesCountersFlush.cpp
    subCacheItemCompile.cpp
    subCacheItemEntitiesCompile.cpp
    subCacheItemGeoCompile.cpp
    subCacheItemEndpointCompile.cpp
    subCachesInit.cpp
    apiModelToCacheSubscription.cpp
    apiModelFromCacheSubscription.cpp
    subCacheItemV2Compile.cpp
    subCacheItemFromDb.cpp
    subCacheV2Match.cpp
    subCacheItemStatsUpdate.cpp
)

include_directories("${PROJECT_SOURCE_DIR}/src/lib")

ADD_LIBRARY(orionld_subCache STATIC ${SOURCES})
