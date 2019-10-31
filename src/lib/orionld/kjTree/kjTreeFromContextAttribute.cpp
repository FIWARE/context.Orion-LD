/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, kjBoolean, ...
}

#include "parseArgs/baStd.h"                                   // BA_FT - for debugging only
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "common/RenderFormat.h"                               // RenderFormat
#include "orionld/common/numberToDate.h"                       // numberToDate
#include "rest/httpHeaderAdd.h"                                // httpHeaderAdd
#include "ngsi/ContextAttribute.h"                             // ContextAttribute
#include "orionld/common/OrionldConnection.h"                  // orionldState
#include "orionld/context/orionldCoreContext.h"                // orionldCoreContext
#include "orionld/context/orionldContextValueLookup.h"         // orionldContextValueLookup
#include "orionld/context/orionldContextCreateFromTree.h"      // orionldContextCreateFromTree
#include "orionld/context/orionldContextListInsert.h"          // orionldContextListInsert
#include "orionld/context/orionldAliasLookup.h"                // orionldAliasLookup
#include "orionld/kjTree/kjTreeFromCompoundValue.h"            // kjTreeFromCompoundValue
#include "orionld/kjTree/kjTreeFromContextAttribute.h"         // Own interface



// -----------------------------------------------------------------------------
//
// ALLOCATION_CHECK -
//
#define ALLOCATION_CHECK(nodeP)                \
  if (nodeP == NULL)                           \
  {                                            \
    *detailsP = (char*) "unable to allocate";  \
    return NULL;                               \
  }



// -----------------------------------------------------------------------------
//
// kjTreeFromContextAttribute -
//
KjNode* kjTreeFromContextAttribute(ContextAttribute* caP, OrionldContext* contextP, RenderFormat renderFormat, char** detailsP)
{
  bool     valueMayBeContracted;
  char*    nameAlias = orionldAliasLookup(contextP, caP->name.c_str(), &valueMayBeContracted);
  KjNode*  nodeP     = NULL;

  if (nameAlias == NULL)
    nameAlias = (char*) caP->name.c_str();

  if (renderFormat == NGSI_LD_V1_KEYVALUES)
  {
    //
    // FIXME: This almost identical switch is in many places. Time to unite ...
    //        KjNode* kjTreeValue(ContextAttribute* aP, const char* fieldName) ?
    //
    switch (caP->valueType)
    {
    case orion::ValueTypeString:
      if (valueMayBeContracted == true)
      {
        char* contractedValue = orionldAliasLookup(contextP, caP->stringValue.c_str(), NULL);

        if (contractedValue != NULL)
          nodeP = kjString(orionldState.kjsonP, nameAlias, contractedValue);
        else
          nodeP = kjString(orionldState.kjsonP, nameAlias, caP->stringValue.c_str());
      }
      else
        nodeP = kjString(orionldState.kjsonP, nameAlias, caP->stringValue.c_str());
      ALLOCATION_CHECK(nodeP);
      break;

    case orion::ValueTypeNumber:
      nodeP = kjFloat(orionldState.kjsonP, nameAlias, caP->numberValue);  // FIXME: kjInteger or kjFloat ...
      ALLOCATION_CHECK(nodeP);
      break;

    case orion::ValueTypeBoolean:
      nodeP = kjBoolean(orionldState.kjsonP, nameAlias, (KBool) caP->boolValue);
      ALLOCATION_CHECK(nodeP);
      break;

    case orion::ValueTypeNull:
      nodeP = kjNull(orionldState.kjsonP, nameAlias);
      ALLOCATION_CHECK(nodeP);
      break;

    case orion::ValueTypeVector:
    case orion::ValueTypeObject:
      nodeP = kjTreeFromCompoundValue(caP->compoundValueP, NULL, valueMayBeContracted, detailsP);
      if (nodeP == NULL)
        return NULL;
      break;

    case orion::ValueTypeNotGiven:
      nodeP = kjString(orionldState.kjsonP, nameAlias, "UNKNOWN TYPE");
      ALLOCATION_CHECK(nodeP);
      break;
    }

    return nodeP;
  }

  KjNode* aTopNodeP = kjObject(orionldState.kjsonP, nameAlias);  // Top node for the attribute

  if (aTopNodeP == NULL)
  {
    *detailsP = (char*) "unable to allocate memory";
    return NULL;
  }

  bool isRelationship = false;
  if (caP->type != "")
  {
    KjNode* typeNodeP = kjString(orionldState.kjsonP, "type", caP->type.c_str());

    if (typeNodeP == NULL)
    {
      *detailsP = (char*) "unable to create KjString node for attribute type";
      free(aTopNodeP);
      return NULL;
    }

    kjChildAdd(aTopNodeP, typeNodeP);
    if (strcmp(typeNodeP->value.s, "Relationship") == 0)
      isRelationship = true;
  }

  // Value
  const char* valueName = (isRelationship == false)? "value" : "object";
  LM_TMP(("NOTIF: valueType of attr '%s': '%s'", caP->name.c_str(), valueTypeName(caP->valueType)));
  switch (caP->valueType)
  {
  case orion::ValueTypeString:
    nodeP = kjString(orionldState.kjsonP, valueName, caP->stringValue.c_str());
    ALLOCATION_CHECK(nodeP);
    break;

  case orion::ValueTypeNumber:
    nodeP = kjFloat(orionldState.kjsonP, "value", caP->numberValue);  // FIXME: kjInteger or kjFloat ...
    ALLOCATION_CHECK(nodeP);
    break;

  case orion::ValueTypeBoolean:
    nodeP = kjBoolean(orionldState.kjsonP, "value", (KBool) caP->boolValue);
    ALLOCATION_CHECK(nodeP);
    break;

  case orion::ValueTypeNull:
    nodeP = kjNull(orionldState.kjsonP, "value");
    ALLOCATION_CHECK(nodeP);
    break;

  case orion::ValueTypeVector:
  case orion::ValueTypeObject:
    nodeP = kjTreeFromCompoundValue(caP->compoundValueP, NULL, valueMayBeContracted, detailsP);
    nodeP->name = (char*) "value";
    if (nodeP == NULL)
      return NULL;
    break;

  case orion::ValueTypeNotGiven:
    nodeP = kjString(orionldState.kjsonP, "value", "UNKNOWN TYPE");
    ALLOCATION_CHECK(nodeP);
    break;
  }
  kjChildAdd(aTopNodeP, nodeP);

  bool isGeoProperty = false;
  LM_TMP(("NOTIF: attribute type is: '%s'", caP->type.c_str()));
  if (strcmp(caP->type.c_str(), "GeoProperty") == 0)
  {
    //
    // GeoProperty attributes seem to get an extra metadata, called "location". It needs to be removed
    //
    LM_TMP(("NOTIF: It's a GeoProperty attribute!"));
    isGeoProperty = true;
  }

  // Metadata
  LM_TMP(("NOTIF: converting %d metadata of attribute '%s' of type '%s'", caP->metadataVector.size(), caP->name.c_str(), caP->type.c_str()));
  for (unsigned int ix = 0; ix < caP->metadataVector.size(); ix++)
  {
    Metadata*   mdP    = caP->metadataVector[ix];
    const char* mdName = mdP->name.c_str();

    if ((isGeoProperty == true) && (strcmp(mdName, "location") == 0))
    {
      //
      // FIXME
      //   Skipping the metadata "location" for GeoProperty attributes - for now
      //   In the future we might want to keep this metadata, but the unit must be looked over (WGS84).
      //   What was default unit in orion v1 is not default for orionld
      //
      LM_TMP(("NOTIF: skipping metadata '%s'", mdName));
      continue;
    }

    LM_TMP(("NOTIF: converting metadata '%s'", mdName));
    //
    // Special case: observedAt - stored as Number but must be served as a string ...
    //                            also, not expanded
    //
    if (strcmp(mdName, "observedAt") == 0)
    {
      char     date[128];
      char*    details;

      if (numberToDate((time_t) mdP->numberValue, date, sizeof(date), &details) == false)
      {
        LM_E(("numberToDate failed: %s", details));
        return NULL;
      }

      nodeP = kjString(orionldState.kjsonP, mdName, date);
    }
    else
    {
      bool    valueMayBeContracted;
      char*   mdLongName     = orionldAliasLookup(contextP, mdName, &valueMayBeContracted);
      KjNode* typeNodeP      = kjString(orionldState.kjsonP, "type", mdP->type.c_str());
      KjNode* valueNodeP     = NULL;

      nodeP = kjObject(orionldState.kjsonP, mdLongName);

      LM_TMP(("NOTIF: metadata '%s' is a '%s'", mdName, valueTypeName(mdP->valueType)));

      kjChildAdd(nodeP, typeNodeP);
      if (strcmp(mdP->type.c_str(), "Relationship") == 0)
      {
        valueNodeP = kjString(orionldState.kjsonP, "object", mdP->stringValue.c_str());
      }
      else if (strcmp(mdP->type.c_str(), "Property") == 0)
      {
        switch (mdP->valueType)
        {
        case orion::ValueTypeString:
          if (valueMayBeContracted == true)
          {
            char* contractedValue = orionldAliasLookup(contextP, mdP->stringValue.c_str(), NULL);

            if (contractedValue != NULL)
              valueNodeP = kjString(orionldState.kjsonP, "value", contractedValue);
            else
              valueNodeP = kjString(orionldState.kjsonP, "value", mdP->stringValue.c_str());
          }
          else
            valueNodeP = kjString(orionldState.kjsonP, "value", mdP->stringValue.c_str());
          break;

        case orion::ValueTypeNumber:
          valueNodeP = kjFloat(orionldState.kjsonP, "value", mdP->numberValue);  // FIXME: kjInteger or kjFloat
          break;

        case orion::ValueTypeBoolean:
          valueNodeP = kjBoolean(orionldState.kjsonP, "value", (KBool) mdP->boolValue);
          break;

        case orion::ValueTypeNull:
          valueNodeP = kjNull(orionldState.kjsonP, "value");
          break;

        case orion::ValueTypeVector:
        case orion::ValueTypeObject:
          valueNodeP = kjTreeFromCompoundValue(mdP->compoundValueP, NULL, valueMayBeContracted, detailsP);
          break;

        case orion::ValueTypeNotGiven:
          valueNodeP = kjString(orionldState.kjsonP, "value", "UNKNOWN TYPE");
          break;
        }
      }
      else
      {
        valueNodeP = kjString(orionldState.kjsonP, "NonSupportedAttributeType", mdP->type.c_str());
      }

      kjChildAdd(nodeP, valueNodeP);
    }

    kjChildAdd(aTopNodeP, nodeP);
  }

  return aTopNodeP;
}
