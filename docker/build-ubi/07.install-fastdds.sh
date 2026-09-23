#!/bin/bash -xe

# Copyright 2024 FIWARE Foundation e.V.
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

echo -e "\e[1;32m Builder: installing DDS Libraries \e[0m"
dnf config-manager --set-enabled powertools

yum -y install tinyxml2-devel boost-devel yaml-cpp-devel yaml-cpp

echo -e "\e[1;32m Builder: installing ASIO for DDS Libraries \e[0m"
wget https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz -O /tmp/asio-1-30-2.tar.gz
tar xzf /tmp/asio-1-30-2.tar.gz -C /tmp
cp -r /tmp/asio-asio-1-30-2/asio/include/* /usr/include/
rm -rf /tmp/asio-1-30-2.tar.gz /tmp/asio-asio-1-30-2
echo -e "\e[1;32m Builder: installed ASIO for DDS Libraries \e[0m"

#
# ⭐ A NEWER CMAKE, AND ON THE PATH OF THIS SCRIPT ONLY.
#
# The v1.2.2 stack needs CMake >= 3.20 and UBI 8 ships 3.15.7. That version
# built the pins this replaces - Fast-CDR 2.3.0, Fast-DDS 3.3.0 - and stops
# dead on the new ones:
#
#   CMake Error at CMakeLists.txt:18 (cmake_minimum_required):
#     CMake 3.20 or higher is required.  You are running version 3.15.7
#
# ⛔ It is NOT installed over the system one, and that is deliberate. Every
# other build step in this image is an older project built by the cmake it has
# always had, and a newer cmake is not a free upgrade for them: CMake 4 dropped
# support for cmake_minimum_required below 3.5 outright. 3.31 is the last of
# the 3.x line, so it is new enough for eProsima and still accepts the rest -
# but nothing else here has to find that out, because nothing else here sees
# it.
#
echo -e "\e[1;32m Builder: installing CMake for the DDS Libraries \e[0m"
CMAKE_VERSION=3.31.6
case "$(uname -m)" in
    aarch64|arm64) CMAKE_ARCH=linux-aarch64 ;;
    *)             CMAKE_ARCH=linux-x86_64  ;;
esac
#
# --https-only, because this URL REDIRECTS: GitHub answers a release download
# with a 302 to its object store, so redirects cannot simply be turned off -
# but every hop can be required to stay on https, which is the actual concern.
#
wget -q --https-only https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.tar.gz -O /tmp/cmake.tar.gz
mkdir -p /opt/cmake
tar xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1
rm -f /tmp/cmake.tar.gz
export PATH=/opt/cmake/bin:$PATH
cmake --version

# Fast-DDS
mkdir /opt/Fast-DDS

#
# foonathan_memory_vendor
#
echo "04.install-fastdds.sh: foonathan_memory_vendor"
cd /opt/Fast-DDS
git clone https://github.com/eProsima/foonathan_memory_vendor.git
cd foonathan_memory_vendor
git checkout v1.4.1
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON
cmake --build . --target install


#
# Fast-CDR
#
echo "04.install-fastdds.sh: Fast-CDR"
cd /opt/Fast-DDS
git clone https://github.com/eProsima/Fast-CDR.git
cd Fast-CDR
git checkout v2.3.6
mkdir build
cd build
cmake ..
cmake --build . --target install


#
# Fast-DDS
#
echo "04.install-fastdds.sh: Fast-DDS"
cd /opt/Fast-DDS
git clone https://github.com/eProsima/Fast-DDS.git
cd Fast-DDS
git checkout v3.6.2
mkdir build
cd build

## Prevent glibc bug: https://stackoverflow.com/questions/30680550/c-gettid-was-not-declared-in-this-scope
#  Seems to be fixed already manually by eProsima
# file_bug="/opt/Fast-DDS/Fast-DDS/src/cpp/utils/threading/threading_pthread.ipp"
# nl=$(grep -n "namespace eprosima" $file_bug | awk -F':' '{print $1 ; exit 0}')
# sed -i "${nl}i #include <unistd.h>\n#include <sys/syscall.h>\n#define gettid() syscall(SYS_gettid)\n" $file_bug

cmake ..
cmake --build . --target install


#
# DDS Dev Utils (2 packages in one)
#
echo "04.install-fastdds.sh: dev-utils"
cd /opt/Fast-DDS
git clone https://github.com/eProsima/dev-utils.git
cd dev-utils
git checkout v1.5.3

echo "04.install-fastdds.sh: cmake_utils"
mkdir -p build/cmake_utils
cd build/cmake_utils
cmake ../../cmake_utils
cmake --build . --target install

echo "04.install-fastdds.sh: cpp_utils"
cd -
mkdir -p build/cpp_utils
cd build/cpp_utils
cmake ../../cpp_utils
cmake --build . --target install


#
# DDS Pipe (3 packages in one)
#
echo "04.install-fastdds.sh: DDS-Pipe"
cd /opt/Fast-DDS
git clone https://github.com/eProsima/DDS-Pipe.git
cd DDS-Pipe
git checkout v1.5.3


echo "04.install-fastdds.sh: ddspipe_core"
cd ddspipe_core
mkdir build
cd build
cmake ..
cmake --build . --target install

echo "04.install-fastdds.sh: ddspipe_participants"
cd ../../ddspipe_participants
mkdir build
cd build
cmake ..
cmake --build . --target install

echo "04.install-fastdds.sh: ddspipe_yaml"
cd ../../ddspipe_yaml
mkdir build
cd build
cmake ..
cmake --build . --target install


#
# DDS Enabler
#
echo "04.install-fastdds.sh: FIWARE-DDS-Enabler"
yum -y install lz4-devel libzstd-devel json-devel

cd /opt/Fast-DDS
git clone https://github.com/eProsima/FIWARE-DDS-Enabler.git
cd FIWARE-DDS-Enabler

#
# A RELEASE TAG AT LAST - this is the move the previous comment here asked for.
#
# The Enabler used to be pinned by COMMIT, at ad19575 "Append action infix to
# action topics" (#29), because the branch eProsima asked us to use
# ('append_action_infix') had been deleted and no release contained that work
# yet. Both v1.2.1 and v1.2.2 contain it now:
#
#   git tag --contains ad19575d8b6691c9d46c53733ac040e6afeca998
#
# so the reason for the commit pin is gone. It mattered: the infix builds the
# action topic names (ACTION_INFIX "/_action/" in
# ddsenabler_participants/include/ddsenabler_participants/Constants.hpp), so a
# tag without it would quietly break DDS actions rather than fail to build.
#
# The versions above are not a set assembled here either. They are the whole of
# v1.2.2's own ddsenabler.repos - Fast CDR 2.3.6, Fast DDS 3.6.2, dev-utils and
# DDS-Pipe 1.5.3, foonathan_memory_vendor 1.4.1 - which is a combination
# eProsima has built and tested together, rather than one we have.
#
git checkout v1.2.2

# ./install_dds_module.sh

echo "04.install-fastdds.sh: ddsenabler_participants"
mkdir -p build/ddsenabler_participants
cd build/ddsenabler_participants
cmake ../../ddsenabler_participants
cmake --build . --target install
cd ../..

echo "04.install-fastdds.sh: ddsenabler_yaml"
mkdir -p build/ddsenabler_yaml
cd build/ddsenabler_yaml
cmake ../../ddsenabler_yaml
cmake --build . --target install
cd ../..
 
echo "04.install-fastdds.sh: ddsenabler"
mkdir -p build/ddsenabler
cd build/ddsenabler
cmake ../../ddsenabler
cmake --build . --target install

echo "04.install-fastdds.sh: DONE"
