#ifndef SRC_LIB_ORIONLD_MONGOC_MONGOCDATASETINSTANCEOPS_H_
#define SRC_LIB_ORIONLD_MONGOC_MONGOCDATASETINSTANCEOPS_H_

/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
* Author: Ken Zangelin
*/

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}



// -----------------------------------------------------------------------------
//
// Direct mongo manipulation of an entity's @datasets.<attr> array. Used by
// DDS action callbacks (lazy create / surgical merge / cleanup of the
// per-goal datasetId instance) but otherwise free of DDS specifics.
//
// Caller is responsible for orionldState (tenantP, kjsonP, kalloc, etc).
// These helpers do NOT go through orionldPatchEntity2, so no NGSI-LD
// subscription dispatch and no TRoE for these writes.
//



// -----------------------------------------------------------------------------
//
// mongocDatasetInstancePush - $push a complete instance into @datasets.<attr>
//
// instanceTree must be an Object in API form (type/datasetId/value/...).
// createdAt and modifiedAt are stamped onto it before the write.
//
extern bool mongocDatasetInstancePush
(
  const char* entityId,
  const char* attrLongName,
  KjNode*     instanceTree
);



// -----------------------------------------------------------------------------
//
// mongocDatasetSubAttrSet - $set a sub-attribute inside an existing instance
//
// arrayFilters: [{ "elem.datasetId": datasetIdStr }] targets the matching
// instance inside the @datasets.<attr> array. Also bumps the instance's
// modifiedAt and the entity's modDate.
//
extern bool mongocDatasetSubAttrSet
(
  const char* entityId,
  const char* attrLongName,
  const char* datasetIdStr,
  const char* subAttrName,
  KjNode*     subAttrTree
);



// -----------------------------------------------------------------------------
//
// mongocDatasetInstancePull - $pull an instance from @datasets.<attr>
//
// Matches by datasetId.
//
extern bool mongocDatasetInstancePull
(
  const char* entityId,
  const char* attrLongName,
  const char* datasetIdStr
);



// -----------------------------------------------------------------------------
//
// mongocDatasetAttrUnset - $unset the whole "@datasets.<attr>" entry
//
// (A $pull of the last instance would leave an empty array; this removes the
// entry entirely.)
//
extern bool mongocDatasetAttrUnset
(
  const char* entityId,
  const char* attrLongName
);

#endif  // SRC_LIB_ORIONLD_MONGOC_MONGOCDATASETINSTANCEOPS_H_
