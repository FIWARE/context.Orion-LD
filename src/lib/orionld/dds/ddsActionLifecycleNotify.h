#ifndef SRC_LIB_ORIONLD_DDS_DDSACTIONLIFECYCLENOTIFY_H_
#define SRC_LIB_ORIONLD_DDS_DDSACTIONLIFECYCLENOTIFY_H_

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
// ddsActionLifecycleNotify -
//
// Called after a successful direct-mongo @datasets write triggered by a DDS
// action notification. Builds an OrionldAlteration for the action-tied
// attribute, dispatches NGSI-LD subscription notifications matching the
// change, and (if --troe is enabled) records a TRoE "Update" entry for the
// attribute.
//
// attributePayload is the attribute fragment representing what changed (in
// API form, named with the FQ attribute name). For lazy-create that's the
// full new instance; for surgical set it's just the sub-attribute that was
// modified; for pull it can be NULL (the instance disappeared - TRoE entry
// is skipped in that case, but the subscription notification still fires).
//
extern void ddsActionLifecycleNotify
(
  const char* entityId,
  const char* entityType,
  const char* attrLongName,
  KjNode*     attributePayload
);

#endif  // SRC_LIB_ORIONLD_DDS_DDSACTIONLIFECYCLENOTIFY_H_
