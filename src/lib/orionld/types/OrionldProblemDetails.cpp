/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                         // KjNode
#include "kjson/kjBuilder.h"                                      // kjObject, kjString, kjChildAdd, ...
#include "kalloc/kaStrdup.h"                                      // kaStrdup
}

#include "orionld/common/orionldState.h"                          // orionldState
#include "orionld/types/OrionldResponseErrorType.h"               // OrionldResponseErrorType
#include "orionld/types/OrionldProblemDetails.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// orionldProblemDetailsFill -
//
void orionldProblemDetailsFill
(
  OrionldProblemDetails*   pdP,
  OrionldResponseErrorType type,
  const char*              title,
  const char*              detail,
  int                      status
)
{
  pdP->type   = type;
  pdP->title  = (char*) title;
  pdP->detail = (char*) detail;
  pdP->status = status;

  orionldState.httpStatusCode = status;
}



// -----------------------------------------------------------------------------
//
// pdField - add value for OrionldProblemDetails::field
//
void pdField(const char* fieldName)
{
  if (fieldName != NULL)
    orionldState.pd.field = kaStrdup(&orionldState.kalloc, fieldName);
}



// -----------------------------------------------------------------------------
//
// pdAttribute - add value for OrionldProblemDetails::attribute
//
void pdAttribute(const char* attrName)
{
  if (attrName != NULL)
    orionldState.pd.attribute = kaStrdup(&orionldState.kalloc, attrName);
}



// -----------------------------------------------------------------------------
//
// pdEntityId - add value for OrionldProblemDetails::entityId
//
void pdEntityId(const char* entityId)
{
  if (entityId != NULL)
    orionldState.pd.entityId = kaStrdup(&orionldState.kalloc, entityId);
}



// -----------------------------------------------------------------------------
//
// pdOldValue - add value for OrionldProblemDetails::oldValue
//
void pdOldValue(const char* oldValue)
{
  if (oldValue != NULL)
    orionldState.pd.oldValue = kaStrdup(&orionldState.kalloc, oldValue);
}



// -----------------------------------------------------------------------------
//
// pdNewValue - add value for OrionldProblemDetails::newValue
//
void pdNewValue(const char* newValue)
{
  if (newValue != NULL)
    orionldState.pd.newValue = kaStrdup(&orionldState.kalloc, newValue);
}



// -----------------------------------------------------------------------------
//
// pdDatasetId - add value for OrionldProblemDetails::datasetId
//
void pdDatasetId(const char* datasetId)
{
  orionldState.pd.datasetId = (char*) datasetId;
}



// ----------------------------------------------------------------------------
//
// pdTreeCreate -
//
KjNode* pdTreeCreate
(
  OrionldResponseErrorType  errorType,
  const char*               title,
  const char*               detail
)
{
  orionldState.pd.title  = (char*) title;
  orionldState.pd.detail = (char*) detail;

  if ((title  != NULL) && (detail != NULL))
  {
    snprintf(orionldState.pd.titleAndDetailBuffer, sizeof(orionldState.pd.titleAndDetailBuffer), "%s: %s", title, detail);
    orionldState.pd.titleAndDetail = orionldState.pd.titleAndDetailBuffer;
  }
  else if (title != NULL)
    orionldState.pd.titleAndDetail = (char*) title;
  else if (detail != NULL)
    orionldState.pd.titleAndDetail = (char*) detail;
  else
    orionldState.pd.titleAndDetail = (char*) "no error info available";

  KjNode* typeP     = kjString(orionldState.kjsonP, "type",    orionldResponseErrorType(errorType));
  KjNode* titleP    = kjString(orionldState.kjsonP, "title",   title);
  KjNode* detailP;

  if ((detail != NULL) && (detail[0] != 0))
    detailP = kjString(orionldState.kjsonP, "detail", detail);
  else
    detailP = kjString(orionldState.kjsonP, "detail", "no detail");


  orionldState.responseTree = kjObject(orionldState.kjsonP, NULL);

  kjChildAdd(orionldState.responseTree, typeP);
  kjChildAdd(orionldState.responseTree, titleP);
  kjChildAdd(orionldState.responseTree, detailP);

  if (orionldState.pd.field != NULL)
  {
    KjNode* fieldP = kjString(orionldState.kjsonP, "field", orionldState.pd.field);
    kjChildAdd(orionldState.responseTree, fieldP);
  }

  if (orionldState.pd.attribute != NULL)
  {
    KjNode* attributeP = kjString(orionldState.kjsonP, "attribute", orionldState.pd.attribute);
    kjChildAdd(orionldState.responseTree, attributeP);
  }

  if (orionldState.pd.entityId != NULL)
  {
    KjNode* entityIdP = kjString(orionldState.kjsonP, "entityId", orionldState.pd.entityId);
    kjChildAdd(orionldState.responseTree, entityIdP);
  }

  if (orionldState.pd.oldValue != NULL)
  {
    KjNode* oldValueP = kjString(orionldState.kjsonP, "oldValue", orionldState.pd.oldValue);
    kjChildAdd(orionldState.responseTree, oldValueP);
  }

  if (orionldState.pd.newValue != NULL)
  {
    KjNode* newValueP = kjString(orionldState.kjsonP, "newValue", orionldState.pd.newValue);
    kjChildAdd(orionldState.responseTree, newValueP);
  }

  if (orionldState.pd.datasetId != NULL)
  {
    KjNode* datasetIdP = kjString(orionldState.kjsonP, "datasetId", orionldState.pd.datasetId);
    kjChildAdd(orionldState.responseTree, datasetIdP);
  }

  return orionldState.responseTree;
}



// ----------------------------------------------------------------------------
//
// pdTreeCreate -
//
KjNode* pdTreeCreate(OrionldProblemDetails* pdP)
{
  KjNode*     typeP   = kjString(orionldState.kjsonP, "type", orionldResponseErrorType(pdP->type));
  KjNode*     titleP  = kjString(orionldState.kjsonP, "title",   pdP->title);
  const char* detail  = ((pdP->detail != NULL) && (pdP->detail[0] != 0))? pdP->detail : "no detail";
  KjNode*     detailP = kjString(orionldState.kjsonP, "detail", detail);

  orionldState.responseTree = kjObject(orionldState.kjsonP, NULL);

  kjChildAdd(orionldState.responseTree, typeP);
  kjChildAdd(orionldState.responseTree, titleP);
  kjChildAdd(orionldState.responseTree, detailP);

  if (pdP->field != NULL)
  {
    KjNode* fieldP = kjString(orionldState.kjsonP, "field", pdP->field);
    kjChildAdd(orionldState.responseTree, fieldP);
  }

  if (pdP->registrationId != NULL)
  {
    KjNode* regIdP = kjString(orionldState.kjsonP, "registrationId", pdP->registrationId);
    kjChildAdd(orionldState.responseTree, regIdP);
  }

  if (pdP->attribute != NULL)
  {
    KjNode* attributeP = kjString(orionldState.kjsonP, "attribute", pdP->attribute);
    kjChildAdd(orionldState.responseTree, attributeP);
  }

  if (orionldState.pd.entityId != NULL)
  {
    KjNode* entityIdP = kjString(orionldState.kjsonP, "entityId", orionldState.pd.entityId);
    kjChildAdd(orionldState.responseTree, entityIdP);
  }

  if (orionldState.pd.oldValue != NULL)
  {
    KjNode* oldValueP = kjString(orionldState.kjsonP, "oldValue", orionldState.pd.oldValue);
    kjChildAdd(orionldState.responseTree, oldValueP);
  }

  if (orionldState.pd.newValue != NULL)
  {
    KjNode* newValueP = kjString(orionldState.kjsonP, "newValue", orionldState.pd.newValue);
    kjChildAdd(orionldState.responseTree, newValueP);
  }

  if (orionldState.pd.datasetId != NULL)
  {
    KjNode* datasetIdP = kjString(orionldState.kjsonP, "datasetId", orionldState.pd.datasetId);
    kjChildAdd(orionldState.responseTree, datasetIdP);
  }

#if 0
  if (pdP->status >= 400)
  {
    KjNode* statusP = kjInteger(orionldState.kjsonP, "status", pdP->status);
    kjChildAdd(orionldState.responseTree, statusP);
  }
#endif

  return orionldState.responseTree;
}
