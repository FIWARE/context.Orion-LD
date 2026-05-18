/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                        // KT_*
#include "kjson/KjNode.h"                                         // KjNode
#include "kjson/kjLookup.h"                                       // kjLookup
#include "kjson/kjBuilder.h"                                      // kjString, kjObject, kjChildAdd, ...
}

#include "orionld/common/orionldState.h"                          // orionldState
#include "orionld/common/orionldError.h"                          // orionldError
#include "orionld/common/traceLevels.h"                           // KTrace levels
#include "orionld/dbModel/dbModelFromApiAttribute.h"              // dbModelFromApiAttribute
#include "orionld/dbModel/dbModelFromApiAttributeDatasetArray.h"  // Own interface



// -----------------------------------------------------------------------------
//
// datasetInstanceLookup -
//
KjNode* datasetInstanceLookup(KjNode* datasetArrayP, const char* datasetId)
{
  for (KjNode* instanceP = datasetArrayP->value.firstChildP;  instanceP != NULL; instanceP = instanceP->next)
  {
    KjNode* datasetIdP = kjLookup(instanceP, "datasetId");

    if (datasetIdP != NULL)
    {
      if (strcmp(datasetIdP->value.s, datasetId) == 0)
        return instanceP;
    }
  }

  return NULL;
}


extern void attrNameAdd(KjNode* attrNames, const char* name);
// -----------------------------------------------------------------------------
//
// dbModelFromApiAttributeDatasetArray -
//
bool dbModelFromApiAttributeDatasetArray
(
  KjNode* attrArrayP,
  KjNode* dbAttrsP,
  KjNode* attrAddedV,
  KjNode* attrRemovedV,
  bool*   ignoreP,
  KjNode* dbDatasetArray,
  char*   attrDotName
)
{
  bool     defaultFound        = false;
  KjNode*  datasetArrayP       = NULL;
  KjNode*  dbDatasetArrayP     = NULL;
  KjNode*  dbDatasetInstanceP  = NULL;
  char*    attrNameEq          = attrArrayP->name;

  //
  // Allocate (if needed) the datasets object "orionldState.datasets"
  //
  if (orionldState.datasets == NULL)
  {
    orionldState.datasets = kjObject(orionldState.kjsonP, "@datasets");
    if (orionldState.datasets == NULL)
    {
      orionldError(OrionldInternalError, "Out of memory", "allocating dataset object for an entity", 500);
      return false;
    }
  }
  else
    datasetArrayP = kjLookup(orionldState.datasets, attrNameEq);

  if (dbDatasetArray != NULL)
  {
    dbDatasetArrayP = kjLookup(dbDatasetArray, attrNameEq);

    //
    // datasetInstanceLookup targets the URL-param datasetId (used by
    // endpoints like DELETE attr?datasetId=...). Body-level datasetId
    // patches go through this function too but don't carry a URL param -
    // calling the lookup with NULL crashes inside strcmp. The right
    // gates are: there's a DB array to search AND a URL key to match.
    //
    if ((dbDatasetArrayP != NULL) && (orionldState.uriParams.datasetId != NULL))
      dbDatasetInstanceP = datasetInstanceLookup(dbDatasetArrayP, orionldState.uriParams.datasetId);
  }

  //
  // datasetArrayP holds the new/updated instances for this attribute inside
  // orionldState.datasets. Both the "no @datasets in DB yet" path and the
  // "DB already has @datasets for some attrs but not necessarily this one"
  // path may reach here with datasetArrayP still NULL - allocate it now so
  // the per-instance loop below can safely move instances into it.
  //
  if (datasetArrayP == NULL)
  {
    datasetArrayP = kjArray(orionldState.kjsonP, attrNameEq);

    if (datasetArrayP == NULL)
    {
      orionldError(OrionldInternalError, "Out of memory", "allocating dataset array for an attribute", 500);
      return false;
    }

    kjChildAdd(orionldState.datasets, datasetArrayP);
  }


  KjNode*  attrInstanceP = attrArrayP->value.firstChildP;
  KjNode*  next;
  while (attrInstanceP != NULL)
  {
    next = attrInstanceP->next;

    KT_T(KtDbModel, "Attribute: %s (JSON type: %s)", attrArrayP->name, kjValueType(attrInstanceP->type));

    KjNode* datasetIdNodeP = kjLookup(attrInstanceP, "datasetId");

    if (datasetIdNodeP == NULL)  // Default instance
    {
      if (defaultFound == true)  // Can only have one instance without datasetId
      {
        orionldError(OrionldBadRequestData, "More than one attribute instances without datasetId", attrInstanceP->name, 400);
        return false;
      }

      if (dbModelFromApiAttribute(attrInstanceP, dbAttrsP, attrAddedV, attrRemovedV, ignoreP, false, dbDatasetArray) == false)
        return false;

      defaultFound = true;
    }
    else
    {
      double createdAt = orionldState.requestTime;

      // If an old instance with this datasetId exists, get the createdAt and reuse it
      if (dbDatasetInstanceP != NULL)
      {
        KjNode* createdAtP = kjLookup(dbDatasetInstanceP, "createdAt");
        if (createdAtP != NULL)
          createdAt = createdAtP->value.f;
      }

      kjChildRemove(attrArrayP, attrInstanceP);  // Remove the attribute instance from the attribute ...
      kjChildAdd(datasetArrayP, attrInstanceP);  // ... And insert it under @datasets::attrName

      //
      // The DB Model for datasetId instances is just as the API - only need to add the timestamps
      // If any of the timestamps are already present in the incoing payload ...
      // Should they be removed here or are they already removed by pCheckAttribute?
      // For now, I remove them here - then we'll see ...
      //
      KjNode* createdAtP  = kjLookup(attrInstanceP, "createdAt");
      KjNode* modifiedAtP = kjLookup(attrInstanceP, "modifiedAt");

      if (createdAtP  != NULL) kjChildRemove(attrInstanceP, createdAtP);
      if (modifiedAtP != NULL) kjChildRemove(attrInstanceP, modifiedAtP);

      createdAtP  = kjFloat(orionldState.kjsonP, "createdAt",  createdAt);
      modifiedAtP = kjFloat(orionldState.kjsonP, "modifiedAt", orionldState.requestTime);

      kjChildAdd(attrInstanceP, createdAtP);
      kjChildAdd(attrInstanceP, modifiedAtP);
    }

    attrInstanceP = next;
  }

  // No datasets?
  if (datasetArrayP->value.firstChildP == NULL)
    kjChildRemove(orionldState.datasets, datasetArrayP);

  //
  // The array of the attribute now needs to be an object - just the default attribute is left in there -
  // all other instances (with datasetId) have been moved to datasetArrayP (that's inside orionldState.datasets).
  // Unless it is empty of course ...
  //
  if (attrArrayP->value.firstChildP != NULL)
  {
    attrArrayP->type  = attrArrayP->value.firstChildP->type;
    attrArrayP->value = attrArrayP->value.firstChildP->value;
  }

  return true;
}

