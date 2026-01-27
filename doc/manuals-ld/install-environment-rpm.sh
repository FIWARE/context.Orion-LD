#!/bin/bash
# Copyright 2022 Telefonica Investigacion y Desarrollo, S.A.U
#
# This file is part of Orion Context Broker.
#
# Orion Context Broker is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# Orion Context Broker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
# General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
#
# For those usages not covered by this license please contact with
# iot_support at tid dot es
#
# RPM-based distributions (RHEL, CentOS, AlmaLinux, Rocky Linux, Fedora)
# Based on docker/build-ubi scripts
#

set -e

# Configuration
INSTALL_DDS=${INSTALL_DDS:-false}           # Set to true to install DDS support
INSTALL_TESTS=${INSTALL_TESTS:-false}       # Set to true to install test dependencies
K_LIBS_VERSION="release/0.10"
MONGO_C_DRIVER_VERSION="2.2.0"
LIBMICROHTTPD_VERSION="0.9.75"
RAPIDJSON_VERSION="1.0.2"
PAHO_VERSION="v1.3.1"
PROMETHEUS_VERSION="release-0.1.3"
GNUTLS_VERSION="3.8.8"
GMOCK_VERSION="1.5.0"

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

# Detect package manager (yum or dnf)
detect_pkg_manager() {
    if command -v dnf &> /dev/null; then
        echo "dnf"
    else
        echo "yum"
    fi
}

PKG_MGR=$(detect_pkg_manager)

# ============================================================================
# Installation functions
# ============================================================================

install_build_tools() {
    log_step "Installing ${RED}build tools${NC}"
    sudo $PKG_MGR -y install \
        bzip2 ca-certificates python3 python2 cmake curl wget git make \
        gcc-c++ >/dev/null 2>>$LOGFILE
    log_done
}

install_libraries() {
    log_step "Installing ${RED}dependency libraries${NC}"
    sudo $PKG_MGR -y install \
        libcurl-devel libgcrypt-devel zlib-devel openssl-devel \
        libuuid-devel cyrus-sasl-devel libicu libicu-devel \
        boost-devel >/dev/null 2>>$LOGFILE
    log_done
}

install_epel() {
    log_step "Installing ${RED}EPEL repository${NC}"
    sudo $PKG_MGR -y install epel-release >/dev/null 2>>$LOGFILE || true
    log_done
}

install_mongo_legacy_driver() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}MongoDB legacy C++ driver${NC}"

    sudo mkdir -p /opt/mongoclient >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/mongoclient >/dev/null 2>>$LOGFILE
    cd /opt/mongoclient >/dev/null 2>>$LOGFILE

    wget https://github.com/mongodb/mongo-cxx-driver/archive/legacy-1.1.2.tar.gz >/dev/null 2>>$LOGFILE
    tar xfvz legacy-1.1.2.tar.gz >/dev/null 2>>$LOGFILE
    cd mongo-cxx-driver-legacy-1.1.2 >/dev/null 2>>$LOGFILE
    sudo $PKG_MGR -y install scons >/dev/null 2>>$LOGFILE
    scons install --prefix=/usr/local >/dev/null 2>>$LOGFILE
    log_done
}

install_mongo_c_driver() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}mongo-c-driver ${MONGO_C_DRIVER_VERSION}${NC}"

    # Need newer cmake for mongo-c-driver 2.x
    log_done
    log_step "Installing ${RED}CMake 3.15${NC} (required for mongo-c-driver)"
    cd /tmp >/dev/null 2>>$LOGFILE
    wget https://cmake.org/files/v3.15/cmake-3.15.7.tar.gz >/dev/null 2>>$LOGFILE
    tar zxvf cmake-3.15.7.tar.gz >/dev/null 2>>$LOGFILE
    cd cmake-3.15.7 >/dev/null 2>>$LOGFILE
    ./bootstrap --prefix=/usr/local >/dev/null 2>>$LOGFILE
    make -j$(nproc) >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done

    log_step "Installing ${RED}mongo-c-driver ${MONGO_C_DRIVER_VERSION}${NC}"
    sudo mkdir -p /opt/mongoc >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/mongoc >/dev/null 2>>$LOGFILE
    cd /opt/mongoc >/dev/null 2>>$LOGFILE

    wget https://github.com/mongodb/mongo-c-driver/releases/download/${MONGO_C_DRIVER_VERSION}/mongo-c-driver-${MONGO_C_DRIVER_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    tar xzf mongo-c-driver-${MONGO_C_DRIVER_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    cd mongo-c-driver-${MONGO_C_DRIVER_VERSION} >/dev/null 2>>$LOGFILE
    mkdir -p cmake-build >/dev/null 2>>$LOGFILE
    cd cmake-build >/dev/null 2>>$LOGFILE
    /usr/local/bin/cmake -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF .. >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_gnutls() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}GnuTLS ${GNUTLS_VERSION}${NC}"

    sudo $PKG_MGR -y install libtasn1-devel p11-kit-devel libunistring-devel >/dev/null 2>>$LOGFILE

    sudo mkdir -p /opt/gnutls >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/gnutls >/dev/null 2>>$LOGFILE
    cd /opt/gnutls >/dev/null 2>>$LOGFILE

    wget https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/gnutls-${GNUTLS_VERSION}.tar.xz >/dev/null 2>>$LOGFILE
    tar xvf gnutls-${GNUTLS_VERSION}.tar.xz >/dev/null 2>>$LOGFILE
    cd gnutls-${GNUTLS_VERSION} >/dev/null 2>>$LOGFILE
    ./configure --with-included-libtasn1 --with-included-unistring --without-p11-kit --disable-doc >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_libmicrohttpd() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}libmicrohttpd ${LIBMICROHTTPD_VERSION}${NC}"

    sudo mkdir -p /opt/libmicrohttpd >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/libmicrohttpd >/dev/null 2>>$LOGFILE
    cd /opt/libmicrohttpd >/dev/null 2>>$LOGFILE

    wget https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    tar xvf libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz >/dev/null 2>>$LOGFILE
    cd libmicrohttpd-${LIBMICROHTTPD_VERSION} >/dev/null 2>>$LOGFILE
    ./configure --disable-messages --disable-postprocessor --disable-dauth >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_rapidjson() {
    local GROUP=$(get_group)
    log_step "Installing ${RED}rapidjson ${RAPIDJSON_VERSION}${NC}"

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

    # Clone all k-libs
    for kproj in kbase ktrace klog kargs kalloc khash kjson; do
        log_step "Cloning ${RED}${kproj}${NC}"
        cd ~/git >/dev/null 2>>$LOGFILE
        if [ -d "$kproj" ]; then
            rm -rf $kproj
        fi
        git clone https://gitlab.com/kzangeli/${kproj}.git >/dev/null 2>>$LOGFILE
        log_done
    done

    # Build and install in correct order
    for kproj in kbase ktrace klog kargs kalloc khash kjson; do
        log_step "Building and installing ${RED}${kproj}${NC}"
        cd ~/git/${kproj} >/dev/null 2>>$LOGFILE
        git checkout ${K_LIBS_VERSION} >/dev/null 2>>$LOGFILE
        make >/dev/null 2>>$LOGFILE
        sudo make install >/dev/null 2>>$LOGFILE
        log_done
    done
}

install_paho_mqtt() {
    log_step "Installing ${RED}Eclipse Paho MQTT ${PAHO_VERSION}${NC}"

    sudo rm -f /usr/local/lib/libpaho* >/dev/null 2>>$LOGFILE
    cd ~/git >/dev/null 2>>$LOGFILE
    if [ -d "paho.mqtt.c" ]; then
        rm -rf paho.mqtt.c
    fi
    git clone https://github.com/eclipse/paho.mqtt.c.git >/dev/null 2>>$LOGFILE
    cd paho.mqtt.c >/dev/null 2>>$LOGFILE
    git fetch -a >/dev/null 2>>$LOGFILE
    git checkout tags/${PAHO_VERSION} >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_paho_python() {
    log_step "Installing ${RED}paho-mqtt Python library${NC}"
    sudo $PKG_MGR -y install python3-pip >/dev/null 2>>$LOGFILE
    pip3 install paho-mqtt >/dev/null 2>>$LOGFILE
    log_done
}

install_mosquitto() {
    log_step "Installing and enabling ${RED}Eclipse Mosquitto${NC}"
    sudo $PKG_MGR -y install mosquitto >/dev/null 2>>$LOGFILE
    sudo systemctl start mosquitto >/dev/null 2>>$LOGFILE || true
    sudo systemctl enable mosquitto >/dev/null 2>>$LOGFILE || true
    log_done
}

install_prometheus_client() {
    log_step "Installing ${RED}Prometheus C client ${PROMETHEUS_VERSION}${NC}"

    cd ~/git >/dev/null 2>>$LOGFILE
    if [ -d "prometheus-client-c" ]; then
        rm -rf prometheus-client-c
    fi
    git clone https://github.com/digitalocean/prometheus-client-c.git >/dev/null 2>>$LOGFILE
    cd prometheus-client-c >/dev/null 2>>$LOGFILE
    git checkout ${PROMETHEUS_VERSION} >/dev/null 2>>$LOGFILE

    # Fix for MHD_AccessHandlerCallback
    sed 's/\&promhttp_handler,/(MHD_AccessHandlerCallback) \&promhttp_handler,/' promhttp/src/promhttp.c > XXX
    mv XXX promhttp/src/promhttp.c

    ./auto build >/dev/null 2>>$LOGFILE

    # Copy libraries to system location
    sudo cp promhttp/build/libpromhttp.so prom/build/libprom.so /usr/local/lib/ >/dev/null 2>>$LOGFILE
    sudo ldconfig >/dev/null 2>>$LOGFILE
    log_done
}

install_postgres_client() {
    log_step "Installing ${RED}PostgreSQL client libraries${NC}"

    # Add PostgreSQL repo
    sudo $PKG_MGR -y install https://download.postgresql.org/pub/repos/yum/reporpms/EL-8-x86_64/pgdg-redhat-repo-latest.noarch.rpm >/dev/null 2>>$LOGFILE || true

    sudo $PKG_MGR -y install yum-utils >/dev/null 2>>$LOGFILE || true

    # Disable default postgresql module if using dnf
    if [ "$PKG_MGR" = "dnf" ]; then
        sudo dnf -y module disable postgresql >/dev/null 2>>$LOGFILE || true
    fi

    sudo $PKG_MGR -y install postgresql13 postgresql13-contrib libpqxx-devel postgresql13-devel postgresql13-libs >/dev/null 2>>$LOGFILE || true
    log_done
}

install_fastdds() {
    local GROUP=$(get_group)
    log_section "Installing Fast-DDS (optional)"

    # Enable powertools/crb repo for dependencies
    log_step "Enabling ${RED}PowerTools/CRB repository${NC}"
    if [ "$PKG_MGR" = "dnf" ]; then
        sudo dnf config-manager --set-enabled powertools >/dev/null 2>>$LOGFILE || \
        sudo dnf config-manager --set-enabled crb >/dev/null 2>>$LOGFILE || true
    fi
    log_done

    # Dependencies
    log_step "Installing ${RED}DDS dependencies${NC}"
    sudo $PKG_MGR -y install tinyxml2-devel boost-devel yaml-cpp-devel yaml-cpp \
        lz4-devel libzstd-devel json-devel >/dev/null 2>>$LOGFILE
    log_done

    # ASIO (not in standard repos)
    log_step "Installing ${RED}ASIO${NC}"
    cd /tmp >/dev/null 2>>$LOGFILE
    wget https://ftp.rpmfind.net/linux/opensuse/ports/i586/tumbleweed/repo/oss/i586/asio-devel-1.30.2-1.3.i586.rpm --no-check-certificate >/dev/null 2>>$LOGFILE
    sudo rpm -i --nodeps asio-devel-1.30.2-1.3.i586.rpm >/dev/null 2>>$LOGFILE || true
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
    if [ -d "context.Orion-LD" ]; then
        rm -rf context.Orion-LD
    fi
    git clone https://github.com/FIWARE/context.Orion-LD.git >/dev/null 2>>$LOGFILE
    log_done
}

compile_orionld() {
    local GROUP=$(get_group)
    log_step "Compiling ${RED}Orion-LD${NC}"

    cd ~/git/context.Orion-LD >/dev/null 2>>$LOGFILE

    sudo touch /usr/bin/orionld >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /usr/bin/orionld >/dev/null 2>>$LOGFILE
    sudo touch /etc/init.d/orionld >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /etc/init.d/orionld >/dev/null 2>>$LOGFILE
    sudo touch /etc/default/orionld >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /etc/default/orionld >/dev/null 2>>$LOGFILE

    make install >/dev/null 2>>$LOGFILE
    log_done
}

install_mongodb() {
    log_section "Installing MongoDB Server"

    log_step "Adding MongoDB repository"
    cat <<EOF | sudo tee /etc/yum.repos.d/mongodb-org-4.4.repo >/dev/null 2>>$LOGFILE
[mongodb-org-4.4]
name=MongoDB Repository
baseurl=https://repo.mongodb.org/yum/redhat/\$releasever/mongodb-org/4.4/x86_64/
gpgcheck=1
enabled=1
gpgkey=https://www.mongodb.org/static/pgp/server-4.4.asc
EOF
    log_done

    log_step "Installing MongoDB packages"
    sudo $PKG_MGR -y install mongodb-org >/dev/null 2>>$LOGFILE
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
    sudo $PKG_MGR -y install gdb >/dev/null 2>>$LOGFILE
    log_done

    log_step "Installing ${RED}gmock ${GMOCK_VERSION}${NC}"
    sudo mkdir -p /opt/gmock >/dev/null 2>>$LOGFILE
    sudo chown $USER:$GROUP /opt/gmock >/dev/null 2>>$LOGFILE
    cd /opt/gmock >/dev/null 2>>$LOGFILE

    wget https://src.fedoraproject.org/repo/pkgs/gmock/gmock-${GMOCK_VERSION}.tar.bz2/d738cfee341ad10ce0d7a0cc4209dd5e/gmock-${GMOCK_VERSION}.tar.bz2 >/dev/null 2>>$LOGFILE
    tar xfvj gmock-${GMOCK_VERSION}.tar.bz2 >/dev/null 2>>$LOGFILE
    cd gmock-${GMOCK_VERSION} >/dev/null 2>>$LOGFILE
    ./configure >/dev/null 2>>$LOGFILE
    make >/dev/null 2>>$LOGFILE
    sudo make install >/dev/null 2>>$LOGFILE
    log_done
}

install_functional_test_deps() {
    log_section "Installing Functional Test dependencies"

    log_step "Installing ${RED}nc and bc${NC}"
    sudo $PKG_MGR -y install nc bc >/dev/null 2>>$LOGFILE
    log_done

    log_step "Installing ${RED}python3-virtualenv${NC}"
    sudo $PKG_MGR -y install python3-virtualenv >/dev/null 2>>$LOGFILE || \
    pip3 install virtualenv >/dev/null 2>>$LOGFILE
    log_done

    log_step "Setting up ${RED}Python virtual environment${NC}"
    cd ~/git/context.Orion-LD >/dev/null 2>>$LOGFILE
    python3 -m virtualenv .venv >/dev/null 2>>$LOGFILE || virtualenv -p python3 .venv >/dev/null 2>>$LOGFILE
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
# Main installation
# ============================================================================

rpm_common() {
    log_section "Installing Orion-LD from source code (RPM-based)"
    echo "Log file: $LOGFILE"
    echo "Package manager: $PKG_MGR"
    echo ""

    # Ensure ~/git exists
    mkdir -p ~/git

    # Basic setup
    install_epel
    install_build_tools
    install_libraries

    # Build dependencies from source
    log_section "Building dependencies from source"
    install_mongo_legacy_driver
    install_mongo_c_driver
    install_gnutls
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

    # PostgreSQL client
    log_section "Installing PostgreSQL client"
    install_postgres_client

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

    # Update library cache
    sudo ldconfig

    echo -e "\n${GREEN}Installation complete!${NC}\n"
    echo "You can now run: orionld -fg"
    echo ""
    echo "Note: You may need to set LD_LIBRARY_PATH:"
    echo "  export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:\$LD_LIBRARY_PATH"
    echo ""
}

# ============================================================================
# Entry point
# ============================================================================

check_linux_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/redhat-release ]; then
        echo "rhel"
    else
        echo "unknown"
    fi
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
    echo "  - RHEL 8/9"
    echo "  - CentOS 8 Stream"
    echo "  - AlmaLinux 8/9"
    echo "  - Rocky Linux 8/9"
    echo "  - Fedora"
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

distro=$(check_linux_distro)

case $distro in
    rhel|centos|almalinux|rocky|fedora)
        rpm_common
        ;;
    *)
        echo "Warning: Unrecognized distribution '$distro'"
        echo "Attempting RPM-based installation anyway..."
        rpm_common
        ;;
esac
