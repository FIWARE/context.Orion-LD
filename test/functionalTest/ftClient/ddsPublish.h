#ifndef TEST_FUNCTIONALTEST_FTCLIENT_DDSPUBLISH_H_
#define TEST_FUNCTIONALTEST_FTCLIENT_DDSPUBLISH_H_

/*
*
* Copyright 2024 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: David Campo, Ken Zangelin
*/
extern "C"
{
#include "kjson/KjNode.h"                                   // KjNode
}



// -----------------------------------------------------------------------------
//
// ddsPublishAttribute -
//
extern void ddsPublishAttribute
(
  const char*  topicType,
  const char*  topicName,
  const char*  entityType,
  const char*  entityId,
  const char*  s,
  int          i,
  double       f,
  bool         b
);



// -----------------------------------------------------------------------------
//
// ddsPublishEntity -
//
extern void ddsPublishEntity
(
  const char* topicType,
  const char* topicName,
  const char* entityType,
  const char* entityId,
  KjNode*     entityP
);

#endif  // TEST_FUNCTIONALTEST_FTCLIENT_DDSPUBLISH_H_
