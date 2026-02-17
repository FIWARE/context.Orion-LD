#!/bin/bash

# Copyright 2014 Telefonica Investigacion y Desarrollo, S.A.U
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


set -e

echo
echo -e "\e[1;32m Debian Builder: installing k libs \e[0m"
for kproj in kbase klog kalloc kjson khash kargs ktrace kprom
do
    git clone https://gitlab.com/kzangeli/${kproj}.git ${ROOT_FOLDER}/$kproj
done


function debug()
{
    wd="$1"
    echo In directory $wd
    echo branches:
    git branch
    echo
}



#
# kbase klog kalloc khash
#
for kproj in kbase ktrace klog kargs kalloc khash kjson kprom
do
    cd ${ROOT_FOLDER}/$kproj

    branch=release/0.10
    if [ $kproj = "kprom" ]
    then
        branch=release/0.1.0
    elif [ $kproj = "kjson" ]
    then
        branch=release/0.10.1
    fi
    
    echo checking out $kproj $branch
    git checkout $branch
    make
    make install
    echo "-----------------------------------------------"
done
