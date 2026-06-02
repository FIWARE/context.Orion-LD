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
#include "ktrace/kTrace.h"                                       // KT_*
#include "kalloc/kaAlloc.h"                                      // kaAlloc
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRender.h"                                      // kjFastRender
#include "kjson/kjRenderSize.h"                                  // kjFastRenderSize
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/traceLevels.h"                          // KTrace levels
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/types/OrionldAlteration.h"                     // OrionldAlteration
#include "orionld/notifications/orionldAlterations.h"            // Own interface



// -----------------------------------------------------------------------------
//
// ALTERATION -
//
// datasetId of the changed instance, for datasetId-scoped watchedAttributes
// (syntax "attr@datasetId"). A single-instance patch carries the attribute as
// an object that still holds its 'datasetId' member at this point (it is moved
// into orionldState.datasets later). NULL means the default (no-datasetId)
// instance, or - for a multi-instance Array patch - "unknown" (we can't tell
// which instances from one char*, so we leave it NULL).
#define ALTERATION(altType)                                                                              \
do                                                                                                       \
{                                                                                                        \
  KjNode* _dsNodeP = (attrP->type == KjObject)? kjLookup(attrP, "datasetId") : NULL;                     \
  aeP->alteredAttributeV[ix].alterationType = altType;                                                   \
  aeP->alteredAttributeV[ix].attrName       = attrP->name;                                               \
  aeP->alteredAttributeV[ix].attrNameEq     = attrNameEq;                                                \
  aeP->alteredAttributeV[ix].datasetId      = (_dsNodeP != NULL)? _dsNodeP->value.s : NULL;              \
  ++ix;                                                                                                  \
} while (0)



// -----------------------------------------------------------------------------
//
// kjValuesDiffer -
//
static bool kjValuesDiffer(KjNode* leftAttr, KjNode* rightAttr)
{
  KjNode* left  = kjLookup(leftAttr,  "value");  // "object", "languageMap" ... First lookup "type" ...
  KjNode* right = kjLookup(rightAttr, "value");

  if (left == NULL)  left  = kjLookup(leftAttr,  "object");
  if (left == NULL)  left  = kjLookup(leftAttr,  "languageMap");
  if (right == NULL) right = kjLookup(rightAttr, "object");
  if (right == NULL) right = kjLookup(rightAttr, "languageMap");

  // Patch fragment doesn't touch the value/object/languageMap - the value
  // isn't being changed by this patch (surgical sub-attribute updates, for
  // example). Return false: no value diff.
  if (left == NULL)
    return false;

  // The attribute exists in the DB (caller checked) but somehow has no
  // value/object/languageMap - that's a DB integrity bug worth flagging.
  if (right == NULL)
    KT_RE(true, "Database Error (DB KjNode has no value member)");

  if (left->type != right->type)
    return true;

  KjValueType type = left->type;

  if (type == KjString)   return (strcmp(left->value.s, right->value.s) == 0)? false : true;
  if (type == KjInt)      return (left->value.i == right->value.i)?            false : true;
  if (type == KjFloat)    return (left->value.f == right->value.f)?            false : true;
  if (type == KjBoolean)  return (left->value.b == right->value.b)?            false : true;

  //
  // Compound values ... let's just render the values and do a strcmp on the rendered buffers
  // However, might be an empty array/object  (kjFastRenderSize crashes if the parameter is NULL)
  //
  if ((left->value.firstChildP == NULL) && (right->value.firstChildP == NULL))
    return false;
  else if ((left->value.firstChildP == NULL) || (right->value.firstChildP == NULL))
    return true;

  int   leftBufSize     = kjFastRenderSize(left->value.firstChildP);
  int   rightBufSize    = kjFastRenderSize(right->value.firstChildP);
  char* leftBuf         = kaAlloc(&orionldState.kalloc, leftBufSize);
  char* rightBuf        = kaAlloc(&orionldState.kalloc, rightBufSize);

  kjFastRender(left->value.firstChildP,  leftBuf);
  kjFastRender(right->value.firstChildP, rightBuf);

  if (strcmp(leftBuf, rightBuf) == 0)
    return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// orionldAlterations -
//
// If replace == true, all those attrs in dbAttrsP that are not in attrsP have been DELETED
//
OrionldAlteration* orionldAlterations(char* entityId, char* entityType, KjNode* attrsP, KjNode* dbAttrsP, bool replace)
{
  OrionldAlteration* aeP   = (OrionldAlteration*) kaAlloc(&orionldState.kalloc, sizeof(OrionldAlteration));
  int                attrs = 0;

  aeP->finalApiEntityP = attrsP;

  // Count deleted attributes - if replace
  if ((replace == true) && (dbAttrsP != NULL))  // dbAttrsP might be NULL - if the entity has no attributes in DB
  {
    for (KjNode* dbAttrP = dbAttrsP->value.firstChildP; dbAttrP != NULL; dbAttrP = dbAttrP->next)
    {
      KjNode* attrP = kjLookup(attrsP, dbAttrP->name);

      if (attrP == NULL)  // Present in DB but not in replacing update - the attr is being deleted
        ++attrs;
    }
  }

  if (attrsP != NULL)
  {
    // Count attributes that have been modified (and that survive the modification)
    for (KjNode* attrP = attrsP->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      ++attrs;
    }
  }

  aeP->entityId          = entityId;
  aeP->entityType        = entityType;
  aeP->alteredAttributes = attrs;

  if (aeP->alteredAttributes != 0)
  {
    aeP->alteredAttributeV = (OrionldAttributeAlteration*) kaAlloc(&orionldState.kalloc, attrs * sizeof(OrionldAttributeAlteration));

    int ix = 0;
    for (KjNode* attrP = attrsP->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      KT_T(KtAlt, "Alteration for attribute '%s'", attrP->name);
      char* attrNameEq = kaStrdup(&orionldState.kalloc, attrP->name);  // Must copy to change dot for eq for ...
      dotForEq(attrNameEq);

      if (attrP->type == KjNull)
      {
        ALTERATION(AttributeDeleted);
        continue;
      }

      //
      // Did this PATCH carry a dataset-instance change for this attribute?
      // dbModelFromApiAttributeDatasetArray would have moved the instance
      // out of attrsP into orionldState.datasets, leaving attrP empty here.
      // Classify as AttributeValueChanged - a new/updated dataset instance
      // is a value-level change from a subscriber's standpoint.
      //
      //
      // Multi-instance via datasetId: the patch body carries the attribute
      // either as { ..., datasetId: ..., ... } or as an Array of such
      // instances. dbModelFromApiAttributeDatasetArray moves these out of
      // attrsP into orionldState.datasets later in the pipeline - this
      // function runs BEFORE that, so we detect from the patch tree
      // directly. A new/updated dataset instance is a value-level change
      // from a subscriber's standpoint, regardless of whether the
      // top-level value field actually differs from the default instance.
      //
      if ((attrP->type == KjArray) || (kjLookup(attrP, "datasetId") != NULL))
      {
        ALTERATION(AttributeValueChanged);
        continue;
      }

      if (dbAttrsP != NULL)
      {
        KjNode* dbAttrP = kjLookup(dbAttrsP, attrNameEq);

        if (dbAttrP == NULL)
        {
          ALTERATION(AttributeAdded);
          continue;
        }

        bool valuesDiffer = kjValuesDiffer(attrP, dbAttrP);

        if (valuesDiffer)
          ALTERATION(AttributeValueChanged);
        else
          ALTERATION(AttributeModifiedAtChanged);  // Need to check all metadata - could also be AttributeMetadataChanged
      }
    }

  }

  aeP->next = NULL;

  return aeP;
}
