#!/bin/bash -xe

# Copyright 2024 Telefonica Investigacion y Desarrollo, S.A.U
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

dnf config-manager --set-enabled powertools

yum -y install tinyxml2-devel boost-devel yaml-cpp yaml-cpp-devel
yum -y --nogpgcheck install https://dl.fedoraproject.org/pub/fedora/linux/releases/39/Everything/x86_64/os/Packages/a/asio-devel-1.28.1-2.fc39.x86_64.rpm

# Fast-DDS
mkdir /opt/Fast-DDS


#
# foonathan_memory_vendor
#
cd /opt/Fast-DDS
git clone https://github.com/eProsima/foonathan_memory_vendor.git
cd foonathan_memory_vendor
git checkout master
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON
cmake --build . --target install


#
# Fast-CDR
#
cd /opt/Fast-DDS
git clone https://github.com/eProsima/Fast-CDR.git
cd Fast-CDR
git checkout master
mkdir build
cd build
cmake ..
cmake --build . --target install


#
# Fast-DDS
#
cd /opt/Fast-DDS
git clone https://github.com/eProsima/Fast-DDS.git
cd Fast-DDS
git checkout master
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
cd /opt/Fast-DDS
git clone https://github.com/eProsima/dev-utils.git
cd dev-utils
git checkout main

mkdir -p build/cmake_utils
cd build/cmake_utils
cmake ../../cmake_utils
cmake --build . --target install

cd -
mkdir -p build/cpp_utils
cd build/cpp_utils
cmake ../../cpp_utils
cmake --build . --target install


#
# DDS Pipe (3 packages in one)
#
cd /opt/Fast-DDS
git clone https://github.com/eProsima/DDS-Pipe.git
cd DDS-Pipe
git checkout main


cd ddspipe_core
mkdir build
cd build
cmake ..
cmake --build . --target install

cd ../../ddspipe_participants
mkdir build
cd build
cmake ..
cmake --build . --target install

cd ../../ddspipe_yaml
mkdir build
cd build
cmake ..
cmake --build . --target install


#
# DDS Enabler
#
cd /opt/Fast-DDS
git clone https://github.com/eProsima/FIWARE-DDS-Enabler.git
cd FIWARE-DDS-Enabler

yum -y install lz4-devel libzstd-devel

mkdir -p build/ddsenabler_participants
cd build/ddsenabler_participants
cmake ../../ddsenabler_participants
cmake --build . --target install
cd ../..

mkdir -p build/ddsenabler_yaml
cd build/ddsenabler_yaml
cmake ../../ddsenabler_yaml
cmake --build . --target install
cd ../..

mkdir -p build/ddsenabler
cd build/ddsenabler
cmake ../../ddsenabler
cmake --build . --target install
cd ../..
