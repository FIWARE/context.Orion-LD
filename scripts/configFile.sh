#!/bin/bash

# Copyright 2021 Telefonica Investigacion y Desarrollo, S.A.U
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

declare -A ddsTopicV
typeset -i ddsTopicIx
ddsTopicIx=-1

declare -A ddsServiceV
typeset -i ddsServiceIx
ddsServiceIx=-1

declare -A ddsActionV
typeset -i ddsActionIx
ddsActionIx=-1

declare -A troeV
typeset -i troeIx
troeIx=-1




# -----------------------------------------------------------------------------
#
# usage
#
function usage()
{
  sfile="Usage: "$(basename $0)
  empty=$(echo $sfile | tr 'a-zA-z/0-9.:' ' ')
  echo "$sfile [-u (usage)]"
  echo "$empty [--ddsTopic <topic>,<entity type>,<entity id>,<attribute name>]"
  echo "$empty [--ddsService <topic>,<entity type>,<entity id>,<attribute name>]"
  echo "$empty [--ddsAction <action>,<entity type>,<entity id>,<attribute name>]"
  echo "$empty [--troe <id,idPattern,type1+type2+...typeN,attribute1+attribute2+...attributeN>]"
  echo
  exit $1
}



 typeset -i ix

while [ $# != 0 ]
do
    if [ "$1" == "-u" ]
    then
      usage
    elif [ "$1" == "--ddsTopic" ]
    then
        ddsTopicIx=$ddsTopicIx+1
        ddsTopicV[$ddsTopicIx]="$2"
        shift
        shift
    elif [ "$1" == "--ddsService" ]
    then
        ddsServiceIx=$ddsServiceIx+1
        ddsServiceV[$ddsServiceIx]="$2"
        shift
        shift
    elif [ "$1" == "--ddsAction" ]
    then
        ddsActionIx=$ddsActionIx+1
        ddsActionV[$ddsActionIx]="$2"
        shift
        shift
    elif [ "$1" == "--troe" ]
    then
        troeIx=$troeIx+1
        troeV[$troeIx]="$2"
        shift
        shift
    else
        echo "Unrecognized option: '$1'"
        exit 1
    fi
done

echo '{'
echo '  "dds": {'
echo '    "ddsmodule": {'
echo '      "dds": {'
echo '        "domain": 0,'
echo '        "allowlist": ['
echo '          {'
echo '            "name": "*"'
echo '          }'
echo '        ],'
echo '        "blocklist": ['
echo '          {'
echo '            "name": "add_blocked_topics_list_here"'
echo '          }'
echo '        ]'
echo '      },'
echo '      "topics": {'
echo '        "name": "*",'
echo '        "qos": {'
echo '          "durability": "TRANSIENT_LOCAL",'
echo '          "reliability": "RELIABLE",'
echo '          "history-depth": 20'
echo '        }'
echo '      },'
echo '      "ddsenabler": null,'
echo '      "specs": {'
echo '        "threads": 12,'
echo '        "logging": {'
echo '          "stdout": false,'
echo '          "verbosity": "info"'
echo '        }'
echo '      }'
echo '    },'
echo '    "ngsild": {'
echo '      "topics": {'

#
# DDS Topics
#
if [ $ddsTopicIx -gt -1 ]
then
    ix=0
    while [ $ix -le $ddsTopicIx ]
    do
        items=${ddsTopicV[$ix]}

        topic=$(echo $items | awk -F, '{ print $1 }')
        eType=$(echo $items | awk -F, '{ print $2 }')
        eId=$(echo   $items | awk -F, '{ print $3 }')
        attr=$(echo  $items | awk -F, '{ print $4 }')

        if [ $ix != $ddsTopicIx ]
        then
            comma=','
        else
            comma=''
        fi

        echo '        "'$topic'": {'
        echo '          "entityType": "'$eType'",'
        echo '          "entityId": "'$eId'",'
        echo '          "attribute": "'$attr'"'
        echo '        }'$comma

        ix=$ix+1
    done
fi

echo '      },'
echo '      "services": {'

#
# DDS Services
#
if [ $ddsServiceIx -gt -1 ]
then
    ix=0
    while [ $ix -le $ddsServiceIx ]
    do
        items=${ddsServiceV[$ix]}

        service=$(echo $items | awk -F, '{ print $1 }')
        eType=$(echo $items | awk -F, '{ print $2 }')
        eId=$(echo   $items | awk -F, '{ print $3 }')
        attr=$(echo  $items | awk -F, '{ print $4 }')

        if [ $ix != $ddsServiceIx ]
        then
            comma=','
        else
            comma=''
        fi

        echo '        "'$service'": {'
        echo '          "entityType": "'$eType'",'
        echo '          "entityId": "'$eId'",'
        echo '          "attribute": "'$attr'"'
        echo '        }'$comma

        ix=$ix+1
    done
fi

echo '      },'
echo '      "actions": {'

#
# DDS Actions
#
if [ $ddsActionIx -gt -1 ]
then
    ix=0
    while [ $ix -le $ddsActionIx ]
    do
        items=${ddsActionV[$ix]}

        action=$(echo $items | awk -F, '{ print $1 }')
        eType=$(echo $items | awk -F, '{ print $2 }')
        eId=$(echo   $items | awk -F, '{ print $3 }')
        attr=$(echo  $items | awk -F, '{ print $4 }')

        if [ $ix != $ddsActionIx ]
        then
            comma=','
        else
            comma=''
        fi

        echo '        "'$action'": {'
        echo '          "entityType": "'$eType'",'
        echo '          "entityId": "'$eId'",'
        echo '          "attribute": "'$attr'"'
        echo '        }'$comma

        ix=$ix+1
    done
fi

echo '      }'
echo '    }'
echo '  },'
echo '  "troe": {'

function asArrayItems()
{
    space="$1"
    typeset -i items
    items=0

    for item in $(echo $2 | sed "s/+/ /g")
    do
        items=$items+1
    done

    typeset -i itemNo
    itemNo=0
    for item in $(echo $2 | sed "s/+/ /g")
    do
        echo -n "$space" '"'$item'"'

        itemNo=$itemNo+1
        if [ $itemNo != $items ]
        then
            echo ,
        else
            echo
        fi
    done
}


if [ $troeIx -gt -1 ]
then
    echo '    "filter": ['

    ix=0
    while [ $ix -le $troeIx ]
    do
        items=${troeV[$ix]}

        id=$(echo          $items | awk -F, '{ print $1 }')
        idPattern=$(echo   $items | awk -F, '{ print $2 }')
        type=$(echo        $items | awk -F, '{ print $3 }')
        attributes=$(echo  $items | awk -F, '{ print $4 }')

        if [ "$type" == "" ]
        then
            echo
            echo "   ..."
            echo
            echo "===================================================================="
            echo "ERROR: Entity Type is Mandatory (third item in comma-separated list)"
            echo "===================================================================="
            exit 2
        fi

        if [ $ix != $troeIx ]
        then
            comma=','
        else
            comma=''
        fi

        echo '      {'
        if [ "$id"         != "" ]; then echo '        "id": "'$id'",';                 fi
        if [ "$idPattern"  != "" ]; then echo '        "idPattern": "'$idPattern'",';   fi
        if [ "$attributes" != "" ]
        then
            echo '        "attributes": ['
            asArrayItems '          ' "$attributes"
            echo '        ],'
        fi

        if [ "$type" != "" ]
        then
            echo '        "type": ['
            asArrayItems '          ' "$type"
            echo '        ]'
        fi
        echo '      }'$comma
        ix=$ix+1
    done

    echo "    ]"
fi
    echo '  }'

echo '}'
