#!/bin/bash
# Copyright 2022 FIWARE Foundation e.V.
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
# Author: Fernando Lopez (original)
# Updated: 2025 - Added DDS, Prometheus, updated k-libs to 0.10
#

set -e

# Configuration
INSTALL_DDS=${INSTALL_DDS:-false}           # Set to true to install DDS support
INSTALL_TESTS=${INSTALL_TESTS:-false}       # Set to true to install test dependencies
K_LIBS_VERSION="release/0.10"
GMOCK_VERSION="1.5.0"
MONGO_C_DRIVER_VERSION="2.2.0"
LIBMICROHTTPD_VERSION="0.9.75"
RAPIDJSON_VERSION="1.0.2"
PAHO_VERSION="v1.3.1"
PROMETHEUS_VERSION="release-0.1.3"

# Colors for output
RED='\033[1;31m'
GREEN='\033[1;32m'
BLUE='\033[1;34m'
NC='\033[0m' # No Color

# Logging
pwd=$(pwd)
LOGFILE="$pwd/install-orionld.log"

log_step() {
    echo -n -e "    ⏳ $1..."
}

log_done() {
    echo -e "  ${GREEN}done${NC}"
}

log_section() {
    echo -e "\n${BLUE}$1${NC}\n"
}

# Get user's group
get_group() {
    id | sed 's/(/ /g' | sed 's/)/ /g' | awk '{print $4}'
}

# ============================================================================
# Installation functions
# ============================================================================

install_aptitude() {
    log_step "Installing ${RED}aptitude${NC}"
    sudo apt-get update >/dev/null 2>>$LOGFILE
    sudo apt-get -y install aptitude >/dev/null 2>>$LOGFILE
    log_done
}

install_build_tools() {
    log_step "Installing ${RED}build tools (build-essential, cmake, scons, curl, git, wget)${NC}"
    sudo aptitude -y install build-essential cmake scons curl git wget >/dev/null 2>>$LOGFILE
    log_done
}

install_libraries() {
    log_step "Installing ${RED}dependency libraries${NC}"
    # gnutls-dev was renamed to libgnutls28-dev on 24.04+ (the metapackage no longer exists)
    # libboost-all-dev intentionally omitted: Orion-LD requires Boost <= 1.71, but
    # 26.04 ships 1.90. Build Boost 1.67 from source separately and install to
    # /usr/local before running this script.
    #
    # apt-get rather than aptitude: aptitude's resolver was silently dropping
    # explicitly-listed packages on re-runs (its pkgstates file remembered an
    # earlier libboost-dev purge cascade). apt-get with --no-install-recommends
    # avoids that and won't re-pull libboost-dev via libasio-dev's Recommends.
    #
    # librdkafka-dev intentionally NOT installed here: the apt package hard-
    # Depends on libcurl4-openssl-dev which conflicts with libcurl4-gnutls-dev
    # (the variant Orion-LD's curl-using code is built against). Build
    # librdkafka from source instead — see install_librdkafka below (or do it
    # manually and place headers under /usr/local/include/librdkafka/).
    sudo apt-get install -y --no-install-recommends \
        libssl-dev libgnutls28-dev libcurl4-gnutls-dev libsasl2-dev \
        libgcrypt-dev uuid-dev libz-dev \
        libpq-dev libgeos-dev libicu-dev >/dev/null 2>>$LOGFILE
    log_done
}

install_mongo_legacy_driver() {
    local GROUP=$(get_group)
    # libmongoclient-dev was removed from Debian/Ubuntu after 18.04. Build the
    # FIWARE-Ops fork from source — same approach as docker/build-ubi.
    log_step "Installing ${RED}legacy mongo cxx driver from source${NC}"

    if [ -f /usr/local/lib/libmongoclient.a ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    # The SConstruct is Python 2 only. On 24.04+ we expect a separately built
    # /opt/python2/bin/scons (see comment block at top of this file). Older
    # systems can use the apt-packaged scons.
    local SCONS=scons
    if [ -x /opt/python2/bin/scons ]; then
        SCONS=/opt/python2/bin/scons
    else
        sudo aptitude -y install scons >/dev/null 2>>$LOGFILE
    fi

    sudo mkdir -p /opt/mongo-cxx-legacy >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/mongo-cxx-legacy >/dev/null 2>>$LOGFILE
    cd /opt/mongo-cxx-legacy >/dev/null 2>>$LOGFILE
    if [ -d mongo-cxx-driver ]; then rm -rf mongo-cxx-driver; fi
    git clone https://github.com/FIWARE-Ops/mongo-cxx-driver >/dev/null 2>>$LOGFILE
    cd mongo-cxx-driver >/dev/null 2>>$LOGFILE
    # boost::next was removed from gcc-15's transitive includes; std::next is
    # the C++11 equivalent.
    sed -i 's/boost::next(batch_iter)/std::next(batch_iter)/g' \
        src/mongo/client/command_writer.cpp \
        src/mongo/client/wire_protocol_writer.cpp 2>>$LOGFILE
    $SCONS --disable-warnings-as-errors --use-sasl-client --ssl >/dev/null 2>>$LOGFILE
    sudo $SCONS install --disable-warnings-as-errors --prefix=/usr/local --use-sasl-client --ssl >/dev/null 2>>$LOGFILE
    log_done
}

install_mongo_c_driver() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}mongo-c-driver ${MONGO_C_DRIVER_VERSION}${NC}"

    if [ -f /usr/local/lib/libmongoc2.so ] || [ -f /usr/local/lib/libmongoc-1.0.so ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    sudo mkdir -p /opt/mongoc >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/mongoc >/dev/null 2>>$LOGFILE
    cd /opt/mongoc >/dev/null 2>>$LOGFILE

    wget https://github.com/mongodb/mongo-c-driver/releases/download/${MONGO_C_DRIVER_VERSION}/mongo-c-driver-${MONGO_C_DRIVER_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    tar xzf mongo-c-driver-${MONGO_C_DRIVER_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    cd mongo-c-driver-${MONGO_C_DRIVER_VERSION} >/dev/null 2>>$LOGFILE
    mkdir -p cmake-build >/dev/null 2>>$LOGFILE
    cd cmake-build >/dev/null 2>>$LOGFILE
    cmake -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF .. >/dev/null 2>>$LOGFILE
    cmake --build . >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done
}

install_librdkafka() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}librdkafka 2.13.0${NC}"

    if [ -f /usr/local/include/librdkafka/rdkafka.h ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    # Build from source instead of `apt install librdkafka-dev`: the apt
    # package hard-Depends on libcurl4-openssl-dev, which conflicts with
    # libcurl4-gnutls-dev (Orion-LD's curl flavor).
    sudo mkdir -p /opt/librdkafka >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/librdkafka >/dev/null 2>>$LOGFILE
    cd /opt/librdkafka >/dev/null 2>>$LOGFILE
    if [ ! -d librdkafka ]; then
        git clone https://github.com/confluentinc/librdkafka.git >/dev/null 2>>$LOGFILE
    fi
    cd librdkafka >/dev/null 2>>$LOGFILE
    git checkout v2.13.0 >/dev/null 2>>$LOGFILE
    ./configure --prefix=/usr/local >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    sudo ldconfig >/dev/null 2>>$LOGFILE
    log_done
}

install_libmicrohttpd() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}libmicrohttpd ${LIBMICROHTTPD_VERSION}${NC}"

    # Use the experimental websocket header as the install marker — the lib
    # ships with 0.9.x without --enable-experimental but Orion-LD needs
    # microhttpd_ws.h, so the .so alone isn't a valid done-state.
    if [ -f /usr/local/include/microhttpd_ws.h ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    sudo mkdir -p /opt/libmicrohttpd >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/libmicrohttpd >/dev/null 2>>$LOGFILE
    cd /opt/libmicrohttpd >/dev/null 2>>$LOGFILE

    if [ ! -d libmicrohttpd-${LIBMICROHTTPD_VERSION} ]; then
        wget https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
        tar xvf libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    fi
    cd libmicrohttpd-${LIBMICROHTTPD_VERSION} >/dev/null 2>>$LOGFILE
    # --enable-experimental: enables microhttpd_ws.h (websocket support) used by
    # Orion-LD's src/lib/orionld/ws/. --enable-https for TLS endpoints.
    ./configure --disable-messages --disable-postprocessor --disable-dauth \
        --enable-https --enable-experimental >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_rapidjson() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}rapidjson ${RAPIDJSON_VERSION}${NC}"

    if [ -f /usr/local/include/rapidjson/document.h ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    sudo mkdir -p /opt/rapidjson >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/rapidjson >/dev/null 2>>$LOGFILE
    cd /opt/rapidjson >/dev/null 2>>$LOGFILE

    wget https://github.com/miloyip/rapidjson/archive/v${RAPIDJSON_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    tar xfvz v${RAPIDJSON_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    sudo mv rapidjson-${RAPIDJSON_VERSION}/include/rapidjson/ /usr/local/include >/dev/null 2>>$LOGFILE
    log_done
}

install_k_libs() {
    log_section "Installing K-libs (${K_LIBS_VERSION})"

    mkdir -p ~/git >/dev/null 2>>$LOGFILE

    # All 8 k-libs Orion-LD's CMakeLists references. kprom is on a different
    # release line (0.1.0) — see special-case below.
    local KLIBS="kbase ktrace klog kargs kalloc khash kjson kprom"

    # Clone all k-libs (skip if already cloned)
    for kproj in $KLIBS; do
        log_step "Cloning ${RED}${kproj}${NC}"
        cd ~/git >/dev/null 2>>$LOGFILE
        if [ -d "$kproj/.git" ]; then
            echo -n " (already cloned)"
        else
            # Use sudo for rm: a previous `sudo make install` may have left
            # root-owned files in $kproj/bin/ that the user can't remove.
            sudo rm -rf $kproj >/dev/null 2>>$LOGFILE
            git clone https://gitlab.com/kzangeli/${kproj}.git >/dev/null 2>>$LOGFILE
        fi
        log_done
    done

    # Build and install in correct order. The Makefiles' `install` target only
    # copies the test binary to $kproj/bin/ (no /usr/local writes), so no sudo
    # is needed — using sudo was the cause of the root-owned-bin/ mess.
    for kproj in $KLIBS; do
        log_step "Building and installing ${RED}${kproj}${NC}"
        cd ~/git/${kproj} >/dev/null 2>>$LOGFILE
        local branch=${K_LIBS_VERSION}
        if [ "$kproj" = "kprom" ]; then
            branch=release/0.1.0
        fi
        git checkout $branch >/dev/null 2>>$LOGFILE
        if [ -f lib${kproj}.a ]; then
            echo -n " (already built)"
        else
            make >/dev/null 2>>$LOGFILE
            make install >/dev/null 2>>$LOGFILE
        fi
        log_done
    done
}

install_paho_mqtt() {
    log_step "Installing ${RED}Eclipse Paho MQTT ${PAHO_VERSION}${NC}"

    if [ -f /usr/local/lib/libpaho-mqtt3cs.so.1 ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    sudo rm -f /usr/local/lib/libpaho* >/dev/null 2>>$LOGFILE
    cd ~/git >/dev/null 2>>$LOGFILE
    if [ -d "paho.mqtt.c" ]; then
        rm -rf paho.mqtt.c
    fi
    git clone https://github.com/eclipse/paho.mqtt.c.git >/dev/null 2>>$LOGFILE
    cd paho.mqtt.c >/dev/null 2>>$LOGFILE
    git fetch -a >/dev/null 2>>$LOGFILE
    git checkout tags/${PAHO_VERSION} >/dev/null 2>>$LOGFILE
    # gcc-15 defaults to C23, which made `bool` a keyword. Paho 1.3.x has
    # `typedef unsigned int bool;` in MQTTPacket.h. Force gnu11.
    make CFLAGS=-std=gnu11 >/dev/null 2>>$LOGFILE
    # `make install` depends on the test binaries (test1.c, etc.), which have
    # genuine pre-C23 issues that gcc-15 rejects. Copy the built libs/headers
    # manually instead.
    sudo bash -c '
        cp -d build/output/libpaho-mqtt3a.so*  /usr/local/lib/
        cp -d build/output/libpaho-mqtt3as.so* /usr/local/lib/
        cp -d build/output/libpaho-mqtt3c.so*  /usr/local/lib/
        cp -d build/output/libpaho-mqtt3cs.so* /usr/local/lib/
        cp src/MQTTAsync.h src/MQTTClient.h src/MQTTClientPersistence.h \
           src/MQTTProperties.h src/MQTTReasonCodes.h src/MQTTSubscribeOpts.h \
           /usr/local/include/
        ldconfig
    ' >/dev/null 2>>$LOGFILE
    log_done
}

install_paho_python() {
    log_step "Installing ${RED}paho-mqtt Python library${NC}"
    # On 24.04+ the system Python is PEP 668 "externally managed" — pip refuses
    # to install into it. Use the apt package instead (24.04 ships 1.6.x,
    # 26.04 ships 2.1.x; either is fine for the functional tests).
    sudo aptitude -y install python3-paho-mqtt python3-pip >/dev/null 2>>$LOGFILE
    log_done
}

install_mosquitto() {
    log_step "Installing and enabling ${RED}Eclipse Mosquitto${NC}"
    sudo aptitude -y install mosquitto >/dev/null 2>>$LOGFILE
    sudo systemctl start mosquitto >/dev/null 2>>$LOGFILE
    sudo systemctl enable mosquitto >/dev/null 2>>$LOGFILE
    log_done
}

install_prometheus_client() {
    log_step "Installing ${RED}Prometheus C client ${PROMETHEUS_VERSION}${NC}"

    if [ -f /usr/local/lib/libpromhttp.so ] && [ -f /usr/local/lib/libprom.so ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    cd ~/git >/dev/null 2>>$LOGFILE
    if [ -d "prometheus-client-c" ]; then
        sudo rm -rf prometheus-client-c >/dev/null 2>>$LOGFILE
    fi
    git clone https://github.com/digitalocean/prometheus-client-c.git >/dev/null 2>>$LOGFILE
    cd prometheus-client-c >/dev/null 2>>$LOGFILE
    git checkout ${PROMETHEUS_VERSION} >/dev/null 2>>$LOGFILE

    # Fix for MHD_AccessHandlerCallback (newer libmicrohttpd headers)
    sed -i 's/\&promhttp_handler,/(MHD_AccessHandlerCallback) \&promhttp_handler,/' promhttp/src/promhttp.c
    # CMake 4.x removed the legacy `-v` flag — autolib/build.sh still passes it.
    sed -i 's/build_test cmake -v/build_test cmake/' autolib/build.sh

    ./auto build >/dev/null 2>>$LOGFILE

    # Copy libraries to system location
    sudo cp promhttp/build/libpromhttp.so prom/build/libprom.so /usr/local/lib/ >/dev/null 2>>$LOGFILE
    sudo ldconfig >/dev/null 2>>$LOGFILE
    log_done
}

install_fastdds() {
    local GROUP=$(get_group)
    log_section "Installing Fast-DDS (optional)"

    # Idempotence: the function builds ~10 cmake projects in /opt/Fast-DDS,
    # any of which fails on a re-run because git-clone refuses to clone over
    # an existing tree. If the final artifact (libddsenabler.so) is already
    # in /usr/local, skip the whole section.
    if [ -f /usr/local/lib/libddsenabler.so ]; then
        log_step "Fast-DDS chain"
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    # Dependencies
    log_step "Installing ${RED}DDS dependencies${NC}"
    # nlohmann-json3-dev provides <nlohmann/json.hpp> which FIWARE-DDS-Enabler's
    # public headers include — without it Orion-LD's dds module fails to
    # compile. libjsoncpp-dev is a different library kept for compatibility.
    sudo aptitude -y install libtinyxml2-dev libyaml-cpp-dev libasio-dev \
        liblz4-dev libzstd-dev libjsoncpp-dev nlohmann-json3-dev >/dev/null 2>>$LOGFILE
    log_done

    sudo mkdir -p /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/Fast-DDS >/dev/null 2>>$LOGFILE

    # foonathan_memory_vendor
    log_step "Installing ${RED}foonathan_memory_vendor${NC}"
    cd /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    git clone https://github.com/eProsima/foonathan_memory_vendor.git >/dev/null 2>>$LOGFILE
    cd foonathan_memory_vendor >/dev/null 2>>$LOGFILE
    git checkout v1.3.1 >/dev/null 2>>$LOGFILE
    mkdir -p build && cd build >/dev/null 2>>$LOGFILE
    cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done

    # Fast-CDR
    log_step "Installing ${RED}Fast-CDR${NC}"
    cd /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    git clone https://github.com/eProsima/Fast-CDR.git >/dev/null 2>>$LOGFILE
    cd Fast-CDR >/dev/null 2>>$LOGFILE
    git checkout v2.3.0 >/dev/null 2>>$LOGFILE
    mkdir -p build && cd build >/dev/null 2>>$LOGFILE
    cmake .. >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done

    # Fast-DDS
    log_step "Installing ${RED}Fast-DDS${NC}"
    cd /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    git clone https://github.com/eProsima/Fast-DDS.git >/dev/null 2>>$LOGFILE
    cd Fast-DDS >/dev/null 2>>$LOGFILE
    git checkout v3.3.0 >/dev/null 2>>$LOGFILE
    mkdir -p build && cd build >/dev/null 2>>$LOGFILE
    cmake .. >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done

    # dev-utils
    log_step "Installing ${RED}dev-utils (cmake_utils, cpp_utils)${NC}"
    cd /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    git clone https://github.com/eProsima/dev-utils.git >/dev/null 2>>$LOGFILE
    cd dev-utils >/dev/null 2>>$LOGFILE
    git checkout v1.3.0 >/dev/null 2>>$LOGFILE

    mkdir -p build/cmake_utils && cd build/cmake_utils >/dev/null 2>>$LOGFILE
    cmake ../../cmake_utils >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE

    cd /opt/Fast-DDS/dev-utils >/dev/null 2>>$LOGFILE
    mkdir -p build/cpp_utils && cd build/cpp_utils >/dev/null 2>>$LOGFILE
    cmake ../../cpp_utils >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done

    # DDS-Pipe
    log_step "Installing ${RED}DDS-Pipe${NC}"
    cd /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    git clone https://github.com/eProsima/DDS-Pipe.git >/dev/null 2>>$LOGFILE
    cd DDS-Pipe >/dev/null 2>>$LOGFILE
    git checkout v1.3.0 >/dev/null 2>>$LOGFILE

    cd ddspipe_core >/dev/null 2>>$LOGFILE
    mkdir -p build && cd build >/dev/null 2>>$LOGFILE
    cmake .. >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE

    cd /opt/Fast-DDS/DDS-Pipe/ddspipe_participants >/dev/null 2>>$LOGFILE
    mkdir -p build && cd build >/dev/null 2>>$LOGFILE
    cmake .. >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE

    cd /opt/Fast-DDS/DDS-Pipe/ddspipe_yaml >/dev/null 2>>$LOGFILE
    mkdir -p build && cd build >/dev/null 2>>$LOGFILE
    cmake .. >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done

    # FIWARE-DDS-Enabler
    log_step "Installing ${RED}FIWARE-DDS-Enabler${NC}"
    cd /opt/Fast-DDS >/dev/null 2>>$LOGFILE
    git clone https://github.com/eProsima/FIWARE-DDS-Enabler.git >/dev/null 2>>$LOGFILE
    cd FIWARE-DDS-Enabler >/dev/null 2>>$LOGFILE
    git checkout main >/dev/null 2>>$LOGFILE

    mkdir -p build/ddsenabler_participants && cd build/ddsenabler_participants >/dev/null 2>>$LOGFILE
    cmake ../../ddsenabler_participants >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE

    cd /opt/Fast-DDS/FIWARE-DDS-Enabler >/dev/null 2>>$LOGFILE
    mkdir -p build/ddsenabler_yaml && cd build/ddsenabler_yaml >/dev/null 2>>$LOGFILE
    cmake ../../ddsenabler_yaml >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE

    cd /opt/Fast-DDS/FIWARE-DDS-Enabler >/dev/null 2>>$LOGFILE
    mkdir -p build/ddsenabler && cd build/ddsenabler >/dev/null 2>>$LOGFILE
    cmake ../../ddsenabler >/dev/null 2>>$LOGFILE
    sudo cmake --build . --target install >/dev/null 2>>$LOGFILE
    log_done

    sudo ldconfig >/dev/null 2>>$LOGFILE
}

clone_orionld() {
    log_step "Cloning ${RED}Orion-LD${NC}"
    cd ~/git >/dev/null 2>>$LOGFILE
    # Skip if a clone (or symlink to one) is already present — never delete
    # an existing working tree.
    if [ -e "context.Orion-LD/.git" ]; then
        echo -n " (already present, skipping)"
        log_done
        return
    fi
    git clone https://github.com/FIWARE/context.Orion-LD.git >/dev/null 2>>$LOGFILE
    log_done
}

compile_orionld() {
    local GROUP=$(get_group)
    log_step "Compiling ${RED}Orion-LD${NC}"

    cd ~/git/context.Orion-LD >/dev/null 2>>$LOGFILE

    # `make install` writes to /usr/bin and /etc unprivileged. Pre-create and
    # chown every install target so the dev workflow (re-run make install)
    # never needs sudo. Six targets: orionld + ftClient, each with a binary,
    # init script, and defaults file.
    for f in /usr/bin/orionld /etc/init.d/orionld /etc/default/orionld \
             /usr/bin/ftClient /etc/init.d/ftClient /etc/default/ftClient; do
        sudo touch "$f" >/dev/null 2>>$LOGFILE
        sudo chown $USER:$GROUP "$f" >/dev/null 2>>$LOGFILE
    done

    make install >/dev/null 2>>$LOGFILE
    log_done
}

install_mongodb() {
    log_section "Installing MongoDB Server"

    # If a `mongo44` Docker container is already running on 127.0.0.1:27017,
    # skip the system-wide install. On 26.04 the apt path is broken anyway:
    # mongo 4.4 (the version Orion-LD's tests target) needs libssl1.1 which
    # resolute doesn't ship. Recommend `docker run -d --name mongo44 --restart
    # unless-stopped -p 127.0.0.1:27017:27017 -v mongo44-data:/data/db mongo:4.4`.
    # Need sudo for `docker ps` until the user is in the `docker` group AND
    # has re-logged in (group membership won't propagate to this shell).
    if command -v docker >/dev/null && sudo docker ps --format '{{.Names}}' 2>/dev/null | grep -qx mongo44; then
        log_step "MongoDB"
        echo -n " (mongo44 docker container running, skipping system install)"
        log_done
        return
    fi

    log_step "Installing gnupg and importing MongoDB GPG key"
    sudo apt-get install -y gnupg >/dev/null 2>>$LOGFILE
    # apt-key was removed in 24.04+. Use signed-by= keyring instead.
    sudo mkdir -p /etc/apt/keyrings >/dev/null 2>>$LOGFILE
    wget -qO- https://www.mongodb.org/static/pgp/server-4.4.asc | \
        sudo gpg --dearmor --yes -o /etc/apt/keyrings/mongodb-server-4.4.gpg \
        2>>$LOGFILE
    log_done

    log_step "Creating MongoDB repository list"
    # MongoDB 4.4 (the version Orion-LD's functional tests target) only ships
    # `mongodb-org-server` packages for `focal` (20.04) and `bionic` (18.04).
    # Newer codenames' 4.4 repos exist but only contain mongosh (the shell);
    # the server binaries were stripped. Pin to focal regardless of host —
    # libc6 is backwards-compatible enough that focal binaries run on resolute.
    UBUNTU_CODENAME=focal
    echo "deb [ arch=amd64,arm64 signed-by=/etc/apt/keyrings/mongodb-server-4.4.gpg ] https://repo.mongodb.org/apt/ubuntu ${UBUNTU_CODENAME}/mongodb-org/4.4 multiverse" | sudo tee /etc/apt/sources.list.d/mongodb-org-4.4.list >/dev/null 2>>$LOGFILE
    log_done

    log_step "Updating package database"
    sudo apt-get update >/dev/null 2>>$LOGFILE
    log_done

    log_step "Installing MongoDB packages"
    sudo apt-get install -y mongodb-org >/dev/null 2>>$LOGFILE
    log_done

    log_step "Starting MongoDB daemon"
    sudo systemctl start mongod.service >/dev/null 2>>$LOGFILE
    log_done

    log_step "Enabling MongoDB on boot"
    sudo systemctl enable mongod.service >/dev/null 2>>$LOGFILE
    log_done
}

# ============================================================================
# Test dependencies
# ============================================================================

install_unit_test_deps() {
    local GROUP=$(get_group)
    log_section "Installing Unit Test dependencies (gtest/gmock)"

    log_step "Installing ${RED}gdb${NC}"
    sudo aptitude -y install gdb >/dev/null 2>>$LOGFILE
    log_done

    log_step "Installing ${RED}gmock ${GMOCK_VERSION}${NC}"
    if [ -f /usr/local/lib/libgmock.so ]; then
        echo -n " (already installed, skipping)"
        log_done
        return
    fi

    sudo mkdir -p /opt/gmock >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/gmock >/dev/null 2>>$LOGFILE
    cd /opt/gmock >/dev/null 2>>$LOGFILE

    if [ ! -d gmock-${GMOCK_VERSION} ]; then
        wget https://src.fedoraproject.org/repo/pkgs/gmock/gmock-${GMOCK_VERSION}.tar.bz2/d738cfee341ad10ce0d7a0cc4209dd5e/gmock-${GMOCK_VERSION}.tar.bz2 >/dev/null 2>>$LOGFILE
        tar xfvj gmock-${GMOCK_VERSION}.tar.bz2 >/dev/null 2>>$LOGFILE
    fi
    cd gmock-${GMOCK_VERSION} >/dev/null 2>>$LOGFILE
    # gmock 1.5.0's fuse_gtest_files.py is Python 2 (uses `print` statement).
    # Point the shebang at /opt/python2/bin/python2 (built earlier in the
    # mongo-cxx-legacy step). On systems without /opt/python2, this step would
    # fall through to /usr/bin/env python which fails on resolute.
    if [ -x /opt/python2/bin/python2 ]; then
        sed -i '1c#!/opt/python2/bin/python2' \
            gtest/scripts/fuse_gtest_files.py 2>>$LOGFILE
    fi
    ./configure >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_functional_test_deps() {
    log_section "Installing Functional Test dependencies"

    log_step "Installing ${RED}netcat and bc${NC}"
    sudo aptitude -y install netcat bc >/dev/null 2>>$LOGFILE
    log_done

    log_step "Installing ${RED}python3-virtualenv${NC}"
    sudo aptitude -y install python3-virtualenv >/dev/null 2>>$LOGFILE
    log_done

    log_step "Setting up ${RED}Python virtual environment${NC}"
    cd ~/git/context.Orion-LD >/dev/null 2>>$LOGFILE
    virtualenv -p python3 .venv >/dev/null 2>>$LOGFILE
    . .venv/bin/activate >/dev/null 2>>$LOGFILE
    pip install -r scripts/requirements.txt >/dev/null 2>>$LOGFILE
    deactivate >/dev/null 2>>$LOGFILE
    log_done

    log_step "Creating ${RED}python symlink${NC}"
    sudo ln -sf /usr/bin/python3 /usr/bin/python >/dev/null 2>>$LOGFILE
    log_done

    echo ""
    echo "To run functional tests:"
    echo "  cd ~/git/context.Orion-LD"
    echo "  . .venv/bin/activate"
    echo "  export PATH=\$PATH:\$PWD/scripts"
    echo "  test/functionalTest/testHarness.sh"
    echo ""
}

# ============================================================================
# Main installation for Ubuntu
# ============================================================================

Ubuntu20.04() {
    Ubuntu_common
}

Ubuntu22.04() {
    Ubuntu_common
}

Ubuntu24.04() {
    Ubuntu_common
}

Ubuntu26.04() {
    Ubuntu_common
}

Ubuntu_common() {
    log_section "Installing Orion-LD from source code"
    echo "Log file: $LOGFILE"
    echo ""

    # Ensure ~/git exists
    mkdir -p ~/git

    # Basic setup
    install_aptitude
    install_build_tools
    install_libraries
    install_mongo_legacy_driver

    # Build dependencies from source
    log_section "Building dependencies from source"
    install_mongo_c_driver
    install_librdkafka
    install_libmicrohttpd
    install_rapidjson

    # K-libs
    install_k_libs

    # MQTT
    log_section "Installing MQTT support"
    install_paho_mqtt
    install_paho_python
    install_mosquitto

    # Prometheus
    log_section "Installing Prometheus metrics support"
    install_prometheus_client

    # DDS (optional)
    if [ "$INSTALL_DDS" = true ]; then
        install_fastdds
    else
        echo -e "\n${BLUE}Skipping DDS installation${NC} (use --with-dds to enable)\n"
    fi

    # Orion-LD
    log_section "Building Orion-LD"
    clone_orionld
    compile_orionld

    # MongoDB
    install_mongodb

    # Test dependencies (optional)
    if [ "$INSTALL_TESTS" = true ]; then
        install_unit_test_deps
        install_functional_test_deps
    else
        echo -e "\n${BLUE}Skipping test dependencies${NC} (use --with-tests to enable)\n"
    fi

    echo -e "\n${GREEN}Installation complete!${NC}\n"
    echo "You can now run: orionld -fg"
    echo ""
    echo "Note: If you installed Prometheus client, you may need to set:"
    echo "  export LD_LIBRARY_PATH=~/git/prometheus-client-c/prom/build:~/git/prometheus-client-c/promhttp/build"
    echo ""
}

# ============================================================================
# Entry point
# ============================================================================

check_linux_version() {
    distributor=$(lsb_release -a 2>/dev/null | grep Distributor | awk '{print $3}')
    release=$(lsb_release -a 2>/dev/null | grep Release | awk '{print $2}')
    version="${distributor}${release}"
    echo $version
}

usage() {
    echo "Usage: $0 [--with-dds] [--with-tests]"
    echo ""
    echo "Options:"
    echo "  --with-dds    Install DDS (Fast-DDS) support"
    echo "  --with-tests  Install test dependencies (gtest/gmock for unit tests,"
    echo "                python virtualenv for functional tests)"
    echo ""
    echo "Supported distributions:"
    echo "  - Ubuntu 20.04"
    echo "  - Ubuntu 22.04"
    echo "  - Ubuntu 24.04"
    echo "  - Ubuntu 26.04"
    echo ""
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --with-dds)
            INSTALL_DDS=true
            shift
            ;;
        --with-tests)
            INSTALL_TESTS=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

version=$(check_linux_version)

# Check if we have a function for this version
if declare -f "$version" > /dev/null; then
    eval $version
else
    echo "Unsupported distribution: $version"
    echo ""
    echo "Attempting generic Ubuntu installation..."
    Ubuntu_common
fi
