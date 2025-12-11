/*
*
* Copyright 2025 FIWARE Foundation e.V.
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
#include <string.h>                                              // strcmp

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/datasetAttributeFix.h"                  // datasetAttributeFix
#include "orionld/common/datasetEntityFix.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// datasetEntityFix -
//
void datasetEntityFix(KjNode* entityP)
{
  KjNode* attrP = entityP->value.firstChildP;
  KjNode* next  = NULL;

  while (attrP != NULL)
  {
    next = attrP->next;

    if ((strcmp(attrP->name, "id")    != 0) &&
        (strcmp(attrP->name, "@id")   != 0) &&
        (strcmp(attrP->name, "type")  != 0) &&
        (strcmp(attrP->name, "@type") != 0) &&
        (strcmp(attrP->name, "scope") != 0))
    {
      KT_T(KtDatasetId, "Fixing datasetId for attribute '%s'", attrP->name);
      datasetAttributeFix(entityP, attrP);
    }

    attrP = next;
  }
}
