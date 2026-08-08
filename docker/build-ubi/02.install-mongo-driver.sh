#!/bin/bash

# Copyright 2018 FIWARE Foundation e.V.
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

set -e

#
# boost-devel and scons are needed to build the (legacy) mongo cxx driver.
#
# They used to come from a third-party mirror - one 'okay-release' RPM off
# repo.okay.com.mx, added here and removed again at the end of this script. That
# made every base-image build depend on a single host staying reachable, and it
# times out (Curl error 28) often enough to matter.
#
# boost-devel is in AlmaLinux AppStream, which docker/other-places.repo already
# enables, and scons comes from PyPI.
#
# It has to be a PYTHON 2 scons: mongo-cxx-driver's SConstruct is Python 2 source
# (it uses backtick-repr), and scons runs SConstruct under whichever Python runs
# scons. 3.1.2 is the last release that still supports Python 2. That is why
# 01.install-build-dependencies.sh installs python2 alongside python3.
#
yum -y install --nogpgcheck boost-devel python2-pip
python2 -m pip install --no-cache-dir "scons==3.1.2"

#
# scons ships with a '#!/usr/bin/env python' shebang and RHEL8 has no unversioned
# 'python' on PATH - only python2 and python3 - so it must be pointed at python2.
# The scons RPM this replaced pulled that in as a dependency, which is why the
# problem never showed before.
#
alternatives --set python /usr/bin/python2 2>/dev/null || ln -sf /usr/bin/python2 /usr/bin/python
python --version

echo -e "\e[1;32m Builder: installing mongo cxx driver \e[0m"
git clone https://github.com/FIWARE-Ops/mongo-cxx-driver ${ROOT_FOLDER}/mongo-cxx-driver
cd ${ROOT_FOLDER}/mongo-cxx-driver
scons --disable-warnings-as-errors --use-sasl-client --ssl
scons install --disable-warnings-as-errors --prefix=/usr/local --use-sasl-client --ssl
cd ${ROOT_FOLDER} && rm -Rf mongo-cxx-driver

