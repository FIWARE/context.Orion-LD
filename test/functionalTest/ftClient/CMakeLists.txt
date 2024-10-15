# Copyright 2023 FIWARE Foundation e.V.
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

SET (SOURCES
    ftClient.cpp
    ftErrorResponse.cpp
    mhdRequestInit.cpp
    mhdRequestTreat.cpp
    mhdRequestEnded.cpp
    mhdInit.cpp
    getDump.cpp
    deleteDump.cpp
    die.cpp
    deleteDdsDump.cpp
    getDdsDump.cpp
    postDdsSub.cpp
    postDdsPub.cpp
    ddsPublish.cpp
    NgsildSampleTypeObjectSupport.cxx
    NgsildSamplePubSubTypes.cxx
    NgsildPublisher.cpp
    DdsNotificationSender.cpp
)

SET (COMMON_STATIC_LIBS
    kjson.a
    khash.a
    kalloc.a
    ktrace.a
    kargs.a
    kbase.a
)

SET (ORIONLD_LIBS
    orionld_mhd
    orionld_http
    orionld_serviceRoutines
    orionld_service
    orionld_troe
    orionld_kjTree
    orionld_mqtt
    orionld_dds
    orionld_context
    orionld_contextCache
    orionld_regCache
    orionld_notifications
    orionld_db
    orionld_mongoc
    orionld_db
    orionld_socketService
    orionld_troe
    orionld_mhd
    orionld_dbModel
    orionld_apiModel
    orionld_pernot
    orionld_kjTree
    orionld_payloadCheck
    orionld_q
    orionld_prometheus
    orionld_common
    orionld_context
    orionld_types
    lm
)

SET (STATIC_LIBS
    ${ORIONLD_LIBS}
    ${COMMON_STATIC_LIBS}
)

SET (DYNAMIC_LIBS
  fastdds
  fastcdr
  foonathan_memory-0.7.3
  ddsenabler
  tinyxml2
  crypto.so
  ssl.so
  pthread.so
)

# Include directories
# ------------------------------------------------------------
include_directories("${PROJECT_SOURCE_DIR}/src/app")
include_directories("${PROJECT_SOURCE_DIR}/src/lib")
include_directories("${PROJECT_SOURCE_DIR}/src/lib/orionld")
include_directories("${PROJECT_SOURCE_DIR}/test/functionalTest")


# Lib directories
# ------------------------------------------------------------
link_directories("/usr/local/lib/")
link_directories("/usr/lib64/")
link_directories("/usr/lib/x86_64-linux-gnu")


# Executable declaration
# ------------------------------------------------------------

ADD_EXECUTABLE(ftClient ${SOURCES} ${HEADERS})

TARGET_LINK_LIBRARIES(ftClient ${STATIC_LIBS} ${DYNAMIC_LIBS} -lmicrohttpd)

IF (${DISTRO} MATCHES "Ubuntu.*")
  MESSAGE("ftClient: INSTALL Ubuntu DISTRO: '${DISTRO}'")
  INSTALL(PROGRAMS ${CMAKE_SOURCE_DIR}/etc/init.d/ftClient.ubuntu DESTINATION /etc/init.d RENAME ftClient)
  INSTALL(FILES ${CMAKE_SOURCE_DIR}/etc/config/ftClient DESTINATION /etc/default RENAME ftClient)
ENDIF()

INSTALL(TARGETS               ftClient
        RUNTIME DESTINATION   bin)
