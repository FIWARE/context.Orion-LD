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
* Author: Carsten Frey
*/
#include <string.h>                                            // strcmp, strlen
#include <stdlib.h>                                            // atof, strtol

extern "C"
{
#include "ktrace/kTrace.h"                                     // KT_*
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjString, kjFloat, ...
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/pqHeader.h"                           // PGresult, PQgetvalue, etc.
#include "orionld/troe/pgTemporalEntityBuild.h"                // Own interface



// -----------------------------------------------------------------------------
//
// pgTimestampToIso8601 - convert PostgreSQL timestamp "2024-06-15 10:00:00" to ISO 8601 "2024-06-15T10:00:00Z"
//
static void pgTimestampToIso8601(const char* pgTs, char* iso, int isoSize)
{
  snprintf(iso, isoSize, "%s", pgTs);

  char* space = strchr(iso, ' ');
  if (space != NULL)
    *space = 'T';

  int len = strlen(iso);
  if (len > 0 && len + 1 < isoSize && iso[len - 1] != 'Z')
  {
    iso[len]     = 'Z';
    iso[len + 1] = 0;
  }
}



// -----------------------------------------------------------------------------
//
// Column indices for the attributes query result
//
#define ATTR_COL_ID                 0
#define ATTR_COL_VALUETYPE          1
#define ATTR_COL_TEXT               2
#define ATTR_COL_BOOLEAN            3
#define ATTR_COL_NUMBER             4
#define ATTR_COL_DATETIME           5
#define ATTR_COL_COMPOUND           6
#define ATTR_COL_OBSERVEDAT         7
#define ATTR_COL_UNITCODE           8
#define ATTR_COL_DATASETID          9
#define ATTR_COL_SUBPROPERTIES      10
#define ATTR_COL_GEOPOINT           11
#define ATTR_COL_GEOPOLYGON         12
#define ATTR_COL_GEOMULTIPOINT      13
#define ATTR_COL_GEOMULTIPOLYGON    14
#define ATTR_COL_GEOLINESTRING      15
#define ATTR_COL_GEOMULTILINESTRING 16
#define ATTR_COL_INSTANCEID         17

// Column indices for the sub-attributes query result
#define SUBATTR_COL_ID              0
#define SUBATTR_COL_ATTRINSTANCEID  1
#define SUBATTR_COL_ATTRDATASETID   2
#define SUBATTR_COL_VALUETYPE       3
#define SUBATTR_COL_TEXT            4
#define SUBATTR_COL_BOOLEAN         5
#define SUBATTR_COL_NUMBER          6
#define SUBATTR_COL_DATETIME        7
#define SUBATTR_COL_COMPOUND        8
#define SUBATTR_COL_OBSERVEDAT      9
#define SUBATTR_COL_UNITCODE        10
#define SUBATTR_COL_GEOPOINT        11



// -----------------------------------------------------------------------------
//
// pgValueNodeBuild - build a KjNode for a single value based on valueType
//
static KjNode* pgValueNodeBuild(PGresult* res, int row, int valueTypeCol, int textCol, int boolCol, int numberCol, int datetimeCol, int compoundCol, int geoPointCol)
{
  const char* valueType = PQgetvalue(res, row, valueTypeCol);

  if (strcmp(valueType, "String") == 0)
  {
    const char* text = PQgetvalue(res, row, textCol);
    return kjString(orionldState.kjsonP, "value", text);
  }
  else if (strcmp(valueType, "Number") == 0)
  {
    const char* numStr = PQgetvalue(res, row, numberCol);
    double num = atof(numStr);
    return kjFloat(orionldState.kjsonP, "value", num);
  }
  else if (strcmp(valueType, "Boolean") == 0)
  {
    const char* boolStr = PQgetvalue(res, row, boolCol);
    KBool val = (boolStr[0] == 't') ? KTRUE : KFALSE;
    return kjBoolean(orionldState.kjsonP, "value", val);
  }
  else if (strcmp(valueType, "DateTime") == 0)
  {
    const char* dt = PQgetvalue(res, row, datetimeCol);
    // NGSI-LD DateTime value: { "@type": "DateTime", "@value": "..." }
    KjNode* dtObj = kjObject(orionldState.kjsonP, "value");
    kjChildAdd(dtObj, kjString(orionldState.kjsonP, "@type", "DateTime"));
    kjChildAdd(dtObj, kjString(orionldState.kjsonP, "@value", dt));
    return dtObj;
  }
  else if (strcmp(valueType, "Compound") == 0)
  {
    const char* compoundStr = PQgetvalue(res, row, compoundCol);
    if (compoundStr != NULL && compoundStr[0] != 0)
    {
      KjNode* compoundNode = kjParse(orionldState.kjsonP, (char*) compoundStr);
      if (compoundNode != NULL)
      {
        compoundNode->name = (char*) "value";
        return compoundNode;
      }
    }
    return kjString(orionldState.kjsonP, "value", "");
  }
  else if (strcmp(valueType, "Relationship") == 0)
  {
    const char* text = PQgetvalue(res, row, textCol);
    return kjString(orionldState.kjsonP, "object", text);
  }
  else if (strcmp(valueType, "LanguageMap") == 0)
  {
    const char* compoundStr = PQgetvalue(res, row, compoundCol);
    if (compoundStr != NULL && compoundStr[0] != 0)
    {
      KjNode* langMap = kjParse(orionldState.kjsonP, (char*) compoundStr);
      if (langMap != NULL)
      {
        langMap->name = (char*) "languageMap";
        return langMap;
      }
    }
    return kjString(orionldState.kjsonP, "languageMap", "");
  }
  else if (strncmp(valueType, "Geo", 3) == 0)
  {
    // GeoPoint, GeoPolygon, GeoMultiPoint, etc.
    const char* geoJson = PQgetvalue(res, row, geoPointCol);  // First geo column

    // Try all geo columns to find non-null value
    if (PQgetisnull(res, row, geoPointCol))
    {
      // For attributes result, check further geo columns
      int geoCols[] = { geoPointCol, geoPointCol + 1, geoPointCol + 2, geoPointCol + 3, geoPointCol + 4, geoPointCol + 5 };
      geoJson = NULL;
      for (int i = 0; i < 6; i++)
      {
        if (geoCols[i] < PQnfields(res) && !PQgetisnull(res, row, geoCols[i]))
        {
          geoJson = PQgetvalue(res, row, geoCols[i]);
          break;
        }
      }
    }

    if (geoJson != NULL && geoJson[0] != 0)
    {
      KjNode* geoNode = kjParse(orionldState.kjsonP, (char*) geoJson);
      if (geoNode != NULL)
      {
        geoNode->name = (char*) "value";
        return geoNode;
      }
    }
    return kjString(orionldState.kjsonP, "value", "");
  }

  // Fallback
  const char* text = PQgetvalue(res, row, textCol);
  return kjString(orionldState.kjsonP, "value", (text != NULL) ? text : "");
}



// -----------------------------------------------------------------------------
//
// pgAttrTypeString - map TRoE valueType to NGSI-LD attribute type string
//
static const char* pgAttrTypeString(const char* valueType)
{
  if (strcmp(valueType, "Relationship") == 0)
    return "Relationship";

  if (strncmp(valueType, "Geo", 3) == 0)
    return "GeoProperty";

  if (strcmp(valueType, "LanguageMap") == 0)
    return "LanguageProperty";

  return "Property";
}



// -----------------------------------------------------------------------------
//
// pgTemporalEntityBuild -
//
// Builds a Temporal Representation of an Entity per ETSI GS CIM 009 (Clause 4.5.6).
// Each attribute is an array of instances, where each instance has its own
// type, value, observedAt, instanceId, unitCode, datasetId, and sub-attributes.
//
KjNode* pgTemporalEntityBuild
(
  PGresult*  entityRes,
  PGresult*  attrRes,
  PGresult*  subAttrRes
)
{
  if (PQntuples(entityRes) == 0)
    return NULL;

  // Build entity object with id and type
  const char* entityId   = PQgetvalue(entityRes, 0, 0);
  const char* entityType = PQgetvalue(entityRes, 0, 1);

  KjNode* entityP = kjObject(orionldState.kjsonP, NULL);
  kjChildAdd(entityP, kjString(orionldState.kjsonP, "id", entityId));
  kjChildAdd(entityP, kjString(orionldState.kjsonP, "type", entityType));

  //
  // Build attribute arrays.
  // Rows are ordered by (id, datasetid, ts), so we group consecutive rows
  // with the same attribute name into a single array.
  //
  int          attrRows        = PQntuples(attrRes);
  const char*  currentAttrName = NULL;
  KjNode*      currentArray    = NULL;

  for (int row = 0; row < attrRows; row++)
  {
    const char* attrId    = PQgetvalue(attrRes, row, ATTR_COL_ID);
    const char* valueType = PQgetvalue(attrRes, row, ATTR_COL_VALUETYPE);
    const char* datasetId = PQgetvalue(attrRes, row, ATTR_COL_DATASETID);

    // New attribute name? Start a new array
    if (currentAttrName == NULL || strcmp(attrId, currentAttrName) != 0)
    {
      if (currentArray != NULL)
        kjChildAdd(entityP, currentArray);

      currentArray    = kjArray(orionldState.kjsonP, attrId);
      currentAttrName = attrId;
    }

    // Build instance object
    KjNode* instanceP = kjObject(orionldState.kjsonP, NULL);
    kjChildAdd(instanceP, kjString(orionldState.kjsonP, "type", pgAttrTypeString(valueType)));

    // Add value
    KjNode* valueNodeP = pgValueNodeBuild(attrRes, row, ATTR_COL_VALUETYPE,
                                           ATTR_COL_TEXT, ATTR_COL_BOOLEAN,
                                           ATTR_COL_NUMBER, ATTR_COL_DATETIME,
                                           ATTR_COL_COMPOUND, ATTR_COL_GEOPOINT);
    if (valueNodeP != NULL)
      kjChildAdd(instanceP, valueNodeP);

    // Add observedAt if present (convert PG timestamp to ISO 8601)
    if (!PQgetisnull(attrRes, row, ATTR_COL_OBSERVEDAT))
    {
      const char* observedAt = PQgetvalue(attrRes, row, ATTR_COL_OBSERVEDAT);
      if (observedAt[0] != 0)
      {
        char isoTime[64];
        pgTimestampToIso8601(observedAt, isoTime, sizeof(isoTime));
        kjChildAdd(instanceP, kjString(orionldState.kjsonP, "observedAt", isoTime));
      }
    }

    // Add instanceId if present
    if (!PQgetisnull(attrRes, row, ATTR_COL_INSTANCEID))
    {
      const char* instanceId = PQgetvalue(attrRes, row, ATTR_COL_INSTANCEID);
      if (instanceId[0] != 0)
        kjChildAdd(instanceP, kjString(orionldState.kjsonP, "instanceId", instanceId));
    }

    // Add unitCode if present
    if (!PQgetisnull(attrRes, row, ATTR_COL_UNITCODE))
    {
      const char* unitCode = PQgetvalue(attrRes, row, ATTR_COL_UNITCODE);
      if (unitCode[0] != 0)
        kjChildAdd(instanceP, kjString(orionldState.kjsonP, "unitCode", unitCode));
    }

    // Add datasetId if non-empty and not the default value
    if (datasetId[0] != 0 && strcmp(datasetId, "@none") != 0 && strcmp(datasetId, "None") != 0)
      kjChildAdd(instanceP, kjString(orionldState.kjsonP, "datasetId", datasetId));

    // Add sub-attributes for this attribute instance
    if (!PQgetisnull(attrRes, row, ATTR_COL_SUBPROPERTIES))
    {
      const char* subProps = PQgetvalue(attrRes, row, ATTR_COL_SUBPROPERTIES);
      if (subProps[0] == 't')  // boolean true in postgres text format
      {
        // Get the instanceId of the current attribute to match sub-attributes
        const char* attrInstanceId = PQgetisnull(attrRes, row, ATTR_COL_INSTANCEID) ? "" : PQgetvalue(attrRes, row, ATTR_COL_INSTANCEID);

        int subAttrRows = PQntuples(subAttrRes);
        for (int sRow = 0; sRow < subAttrRows; sRow++)
        {
          const char* subAttrAttrInstanceId = PQgetvalue(subAttrRes, sRow, SUBATTR_COL_ATTRINSTANCEID);

          // Match sub-attribute to this attribute instance by attrInstanceId
          if (strcmp(subAttrAttrInstanceId, attrInstanceId) != 0)
            continue;

          const char* subAttrId        = PQgetvalue(subAttrRes, sRow, SUBATTR_COL_ID);
          const char* subAttrValueType = PQgetvalue(subAttrRes, sRow, SUBATTR_COL_VALUETYPE);

          KjNode* subAttrNodeP = kjObject(orionldState.kjsonP, subAttrId);
          kjChildAdd(subAttrNodeP, kjString(orionldState.kjsonP, "type", pgAttrTypeString(subAttrValueType)));

          KjNode* subValueP = pgValueNodeBuild(subAttrRes, sRow, SUBATTR_COL_VALUETYPE,
                                                SUBATTR_COL_TEXT, SUBATTR_COL_BOOLEAN,
                                                SUBATTR_COL_NUMBER, SUBATTR_COL_DATETIME,
                                                SUBATTR_COL_COMPOUND, SUBATTR_COL_GEOPOINT);
          if (subValueP != NULL)
            kjChildAdd(subAttrNodeP, subValueP);

          // Sub-attribute observedAt (convert PG timestamp to ISO 8601)
          if (!PQgetisnull(subAttrRes, sRow, SUBATTR_COL_OBSERVEDAT))
          {
            const char* subObservedAt = PQgetvalue(subAttrRes, sRow, SUBATTR_COL_OBSERVEDAT);
            if (subObservedAt[0] != 0)
            {
              char isoTime[64];
              pgTimestampToIso8601(subObservedAt, isoTime, sizeof(isoTime));
              kjChildAdd(subAttrNodeP, kjString(orionldState.kjsonP, "observedAt", isoTime));
            }
          }

          // Sub-attribute unitCode
          if (!PQgetisnull(subAttrRes, sRow, SUBATTR_COL_UNITCODE))
          {
            const char* subUnitCode = PQgetvalue(subAttrRes, sRow, SUBATTR_COL_UNITCODE);
            if (subUnitCode[0] != 0)
              kjChildAdd(subAttrNodeP, kjString(orionldState.kjsonP, "unitCode", subUnitCode));
          }

          kjChildAdd(instanceP, subAttrNodeP);
        }
      }
    }

    kjChildAdd(currentArray, instanceP);
  }

  // Attach the last attribute array
  if (currentArray != NULL)
    kjChildAdd(entityP, currentArray);

  return entityP;
}
