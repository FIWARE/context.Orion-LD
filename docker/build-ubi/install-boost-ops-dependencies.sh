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

OPS_DEPS_CORE=(
  'libcurl4' \
  'libssl1.1' \
  'ca-certificates' \
)

yum install -y http://repo.okay.com.mx/centos/7/x86_64/release/okay-release-1-5.noarch.rpm
yum install -y boost-devel


yum install -y ${TO_INSTALL[@]}

echo
echo -e "\e[1;32m Builder: installing core ops deps \e[0m"
 yum -y install  \
    ${OPS_DEPS_CORE[@]}
