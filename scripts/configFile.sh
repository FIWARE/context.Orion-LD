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

echo '{'
echo '  "ddsmodule": {'
echo '    "domain": 0,'
echo '    "allowlist": {'
echo '      "name": "*"'
echo '    },'
echo '    "blocklist": {'
echo '      "name": "add_blocked_topics_list_here"'
echo '    },'
echo '    "topics": {'
echo '      "name": "*",'
echo '      "qos": {'
echo '        "durability": "TRANSIENT_LOCAL",'
echo '        "history-depth": 5'
echo '      }'
echo '    },'
echo '    "enabler": {'
echo '    },'
echo '    "specs": {'
echo '      "threads": 12,'
echo '      "logging": {'
echo '        "stdout": false,'
echo '        "verbosity": "info"'
echo '      }'
echo '    },'
echo '    "ngsild": {'
echo '      "topics": {'


while [ $# != 0 ]
do
  items=$1
  shift

  topic=$(echo $items | awk -F, '{ print $1 }')
  eType=$(echo $items | awk -F, '{ print $2 }')
  eId=$(echo   $items | awk -F, '{ print $3 }')
  attr=$(echo  $items | awk -F, '{ print $4 }')

  if [ $# != 0 ]
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
done


echo '      }'
echo '    }'
echo '  }'
echo '}'
