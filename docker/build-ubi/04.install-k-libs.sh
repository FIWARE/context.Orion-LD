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

    #
    # ⭐ THE PINS, AND THEY ARE THE REASON THIS FILE IS TOUCHED AT ALL.
    #
    # The k-libs are ours and they move; this image is the only place that has
    # to be told. They drifted once before - the image sat on kjson release/0.11
    # while everything else had moved to 0.11.1, so every image built from it
    # was missing three bug fixes and nothing noticed. That is what the literal
    # "kjson version" assertion in ngsild_version.test exists to catch, and it
    # is what caught this.
    #
    # ⚠ A BRANCH THAT IS NOT NAMED HERE FALLS THROUGH TO release/0.10, so a
    # newly released lib needs a line, not just a new branch on gitlab.
    #
    branch=release/0.10
    if [ $kproj = "kprom" ]
    then
        branch=release/0.1.1
    elif [ $kproj = "kjson" ]
    then
        branch=release/0.14.1
    elif [ $kproj = "kalloc" ]
    then
        branch=release/0.10.2
    elif [ $kproj = "kargs" ]
    then
        branch=release/0.10.1
    elif [ $kproj = "kbase" ]
    then
        branch=release/0.11
    fi
    
    echo checking out $kproj $branch
    git checkout $branch
    make
    make install
    echo "-----------------------------------------------"
done
