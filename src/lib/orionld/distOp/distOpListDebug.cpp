/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include "ktrace/ktTraceLevelCheck.h"                            // ktTraceLevelCheck
}

#include "orionld/types/DistOp.h"                                // DistOp
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/distOp/distOpListDebug.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// distOpListDebug -
//
void distOpListDebug(DistOp* distOpList, const char* what)
{
  if (ktTraceLevelCheck(KtDistOpList) == false)
    return;

  KT_T(KtDistOpList, "Matching registrations (%s):", what);

  if (distOpList == NULL)
    KT_T(KtDistOpList, "   None");

  int ix = 0;
  for (DistOp* distOpP = distOpList; distOpP != NULL; distOpP = distOpP->next)
  {
    KT_T(KtDistOpList, "   DistOp %d: Reg Id: %s", ix, distOpP->regP->regId);
    ++ix;
  }
}



// -----------------------------------------------------------------------------
//
// distOpListDebug2 -
//
void distOpListDebug2(DistOp* distOpP, const char* what)
{
  if (ktTraceLevelCheck(KtDistOpList) == false)
    return;

  KT_T(KtDistOpList, "----- DistOp List: %s", what);

  if (distOpP == NULL)
    KT_T(KtDistOpList, "  None");

  while (distOpP != NULL)
  {
    KT_T(KtDistOpList, "  DistOp ID:         %s", distOpP->id);
    KT_T(KtDistOpList, "  Registration:      %s", (distOpP->regP != NULL)? distOpP->regP->regId : "local DB");
    KT_T(KtDistOpList, "  Operation:         %s", distOpTypes[distOpP->operation]);

    if (distOpP->error == true)
    {
      KT_T(KtDistOpList, "  Error:");
      KT_T(KtDistOpList, "    Title:             %s", distOpP->title);
      KT_T(KtDistOpList, "    Detail:            %s", distOpP->detail);
      KT_T(KtDistOpList, "    Status:            %d", distOpP->httpResponseCode);
    }

    if (distOpP->requestBody != NULL)
    {
      if (distOpP->operation == DoDeleteBatch)
      {
        KT_T(KtDistOpList, "  Entity IDs:");
        for (KjNode* eIdNodeP = distOpP->requestBody->value.firstChildP; eIdNodeP != NULL; eIdNodeP = eIdNodeP->next)
        {
          KT_T(KtDistOpList, "  o %s", eIdNodeP->value.s);
        }
      }
      else
      {
        KT_T(KtDistOpList, "  Attributes:");

        int ix = 0;
        for (KjNode* attrP = distOpP->requestBody->value.firstChildP; attrP != NULL; attrP = attrP->next)
        {
          if ((strcmp(attrP->name, "id") != 0) && (strcmp(attrP->name, "type") != 0))
          {
            KT_T(KtDistOpList, "    Attribute %d:   '%s'", ix, attrP->name);
            ++ix;
          }
        }
      }
    }

    if (distOpP->attrList != NULL)
    {
      KT_T(KtDistOpList, "  URL Attributes:        %d", distOpP->attrList->items);
      for (int ix = 0; ix < distOpP->attrList->items; ix++)
      {
        KT_T(KtDistOpList, "    Attribute %d:   '%s'", ix, distOpP->attrList->array[ix]);
      }
    }

    if (distOpP->attrsParam != NULL)
    {
      KT_T(KtDistOpList, "  URL Attributes:        '%s' (len: %d)", distOpP->attrsParam, distOpP->attrsParamLen);
    }

    if (distOpP->typeList != NULL)
    {
      KT_T(KtDistOpList, "  URL Entity Types:        %d", distOpP->typeList->items);
      for (int ix = 0; ix < distOpP->typeList->items; ix++)
      {
        KT_T(KtDistOpList, "    Entity Type %02d:   '%s'", ix, distOpP->typeList->array[ix]);
      }
    }

    if (distOpP->idList != NULL)
    {
      KT_T(KtDistOpList, "  URL Entity IDs:        %d", distOpP->idList->items);
      for (int ix = 0; ix < distOpP->idList->items; ix++)
      {
        KT_T(KtDistOpList, "    Entity ID %02d:   '%s'", ix, distOpP->idList->array[ix]);
      }
    }

    if (distOpP->entityId != NULL)
      KT_T(KtDistOpList, "  URL Entity ID:         %s", distOpP->entityId);
    if (distOpP->entityIdPattern != NULL)
      KT_T(KtDistOpList, "  URL Entity ID Pattern: %s", distOpP->entityIdPattern);
    if (distOpP->entityType != NULL)
      KT_T(KtDistOpList, "  URL Entity TYPE:       %s", distOpP->entityType);

    KT_T(KtDistOpList, "----------------------------------------");

    distOpP = distOpP->next;
  }

  KT_T(KtDistOpList, "---------------------");
}
