/*
*
* Copyright 2018 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Ken Zangelin
*/
#include <string>
#include <vector>

extern "C"
{
#include "kbase/kStringSplit.h"                                // kStringSplit
#include "kjson/kjBuilder.h"                                   // kjArray, kjChildAdd, ...
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "serviceRoutines/postQueryContext.h"                  // V1 service routine that does the whole work ...
#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "mongoBackend/mongoQueryContext.h"                    // mongoQueryContext
#include "orionld/common/SCOMPARE.h"                           // SCOMPAREx
#include "orionld/common/urlCheck.h"                           // urlCheck
#include "orionld/common/urnCheck.h"                           // urnCheck
#include "orionld/common/qLex.h"                               // qLex
#include "orionld/common/qParse.h"                             // qParse
#include "orionld/common/qTreeToBsonObj.h"                     // qTreeToBsonObj
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/kjTree/kjTreeFromQueryContextResponse.h"     // kjTreeFromQueryContextResponse
#include "orionld/context/orionldCoreContext.h"                // orionldDefaultUrl
#include "orionld/context/orionldUriExpand.h"                  // orionldUriExpand
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/serviceRoutines/orionldGetEntities.h"        // Own Interface



// ----------------------------------------------------------------------------
//
// NGSILD_Q_FILTER - use the new Q-filter for NGSI-LD
//
#define NGSILD_Q_FILTER   1



// ----------------------------------------------------------------------------
//
// orionldGetEntities -
//
// URI params:
// - id
// - idPattern
// - type
// - typePattern - Not possible - ignored (need an exact type name to lookup alias)
// - q
// - mq          - Not interesting for ngsi-ld
// - geometry
// - coordinates
// - georel
// - maxDistance
// - options=keyValues
//
bool orionldGetEntities(ConnectionInfo* ciP)
{
  ParseData    parseData;
  char*        id             = (ciP->uriParam["id"].empty())?          NULL : (char*) ciP->uriParam["id"].c_str();
  char*        type           = (ciP->uriParam["type"].empty())?        (char*) "" : (char*) ciP->uriParam["type"].c_str();
  char*        idPattern      = (ciP->uriParam["idPattern"].empty())?   NULL : (char*) ciP->uriParam["idPattern"].c_str();
  char*        q              = (ciP->uriParam["q"].empty())?           NULL : (char*) ciP->uriParam["q"].c_str();
  char*        attrs          = (ciP->uriParam["attrs"].empty())?       NULL : (char*) ciP->uriParam["attrs"].c_str();

  char*        geometry       = (ciP->uriParam["geometry"].empty())?    NULL : (char*) ciP->uriParam["geometry"].c_str();
  char*        georel         = (ciP->uriParam["georel"].empty())?      NULL : (char*) ciP->uriParam["georel"].c_str();
  char*        coordinates    = (ciP->uriParam["coordinates"].empty())? NULL : (char*) ciP->uriParam["coordinates"].c_str();

  char*        idString       = (id != NULL)? id      : idPattern;
  const char*  isIdPattern    = (id != NULL)? "false" : "true";
  bool         isTypePattern  = (*type != 0)? false : true;

  EntityId*    entityIdP;
  char         typeExpanded[256];
  char*        details;
  char*        idVector[32];
  char*        typeVector[32];
  int          idVecItems   = (int) sizeof(idVector) / sizeof(idVector[0]);
  int          typeVecItems = (int) sizeof(typeVector) / sizeof(typeVector[0]);
  bool         keyValues    = ciP->uriParamOptions[OPT_KEY_VALUES];

  //
  // Preconditions
  // - 1. If neither Entity types nor Attribute names are provided, an error of type BadRequestData shall be raised.
  // - 2. If an Entity type or Entity id is not provided then an error of type BadRequestData shall be raised.
  // - 3. If the list of Entity identifiers includes a URI which it is not valid, or the query or geo-query are not
  //      syntactically valid (as per the referred clauses 4.9 and 4.10) an error of type BadRequestData shall be raised.
  //
  if ((*type == 0) && (attrs == NULL) && (id == NULL))
  {
    LM_W(("Bad Input (too broad query - entity type/id not given nor attribute list)"));

    orionldErrorResponseCreate(OrionldBadRequestData,
                               "too broad query",
                               "entity type/id not given nor attribute list");

    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  if ((*type == 0) && (idPattern == NULL) && (id == NULL))
  {
    LM_W(("Bad Input (too broad query - entity type not given nor entity id)"));

    orionldErrorResponseCreate(OrionldBadRequestData,
                               "too broad query",
                               "entity type not given nor entity id");

    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  if ((idPattern != NULL) && (id != NULL))
  {
    LM_W(("Bad Input (both 'idPattern' and 'id' used)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Incompatible parameters", "id, idPattern");
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // If 'georel' is present, make sure it has a valid value
  //

  if (georel != NULL)
  {
    if ((strncmp(georel, "near", 4)        != 0) &&
        (strncmp(georel, "within", 6)      != 0) &&
        (strncmp(georel, "contains", 8)    != 0) &&
        (strncmp(georel, "overlaps", 8)    != 0) &&
        (strncmp(georel, "intersects", 10) != 0) &&
        (strncmp(georel, "equals", 6)      != 0) &&
        (strncmp(georel, "disjoint", 8)    != 0))
    {
      LM_W(("Bad Input (invalid value for georel)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "invalid value for georel", georel);
      ciP->httpStatusCode = SccBadRequest;
      return false;
    }

    char* georelExtra = strstr(georel, ";");

    if (georelExtra != NULL)
    {
      ++georelExtra;  // Step over ';', but don't "destroy" the string - it is used as is later on

      if ((strncmp(georelExtra, "minDistance==", 11) != 0) && (strncmp(georelExtra, "maxDistance==", 11) != 0))
      {
        LM_W(("Bad Input (invalid value for georel parameter: %s)", georelExtra));
        orionldErrorResponseCreate(OrionldBadRequestData, "invalid value for georel parameter", georel);
        ciP->httpStatusCode = SccBadRequest;
        return false;
      }
    }
  }

  if (geometry != NULL)
  {
    if (coordinates == NULL)
    {
      LM_W(("Bad Input (coordinates missing)"));

      orionldErrorResponseCreate(OrionldBadRequestData,
                                 "no coordinates",
                                 "geometry without coordinates");

      ciP->httpStatusCode = SccBadRequest;
      return false;
    }
    else if (georel == NULL)
    {
      LM_W(("Bad Input (georel missing)"));

      orionldErrorResponseCreate(OrionldBadRequestData,
                                 "no georel",
                                 "geometry with coordinates but without georel");

      ciP->httpStatusCode = SccBadRequest;
      return false;
    }

    Scope*       scopeP = new Scope(SCOPE_TYPE_LOCATION, "");
    std::string  errorString;

    //
    // In APIv2, the vector is a string without [], in NGSI-LD, [] are present. Must remove ...
    //
    if (coordinates[0] == '[')
    {
      ++coordinates;

      int len = strlen(coordinates);
      if (coordinates[len - 1] == ']')
        coordinates[len - 1] = 0;
    }

    if (scopeP->fill(ciP->apiVersion, geometry, coordinates, georel, &errorString) != 0)
    {
      scopeP->release();
      delete scopeP;

      LM_E(("Geo: Scope::fill failed"));
      orionldErrorResponseCreate(OrionldInternalError, "error filling a scope", errorString.c_str());
      ciP->httpStatusCode = SccBadRequest;
      return false;
    }

    LM_E(("Geo: Scope::fill OK"));
    parseData.qcr.res.restriction.scopeVector.push_back(scopeP);
  }

  if (idString == NULL)
  {
    idString    = (char*) ".*";
    isIdPattern = (char*) "true";
  }

  if (*type == 0)  // No type given - match all types
  {
    type          = (char*) ".*";
    isTypePattern = true;
    typeVecItems  = 0;  // Just to avoid entering the "if (typeVecItems == 1)"
  }
  else
    typeVecItems = kStringSplit(type, ',', (char**) typeVector, typeVecItems);

  idVecItems   = kStringSplit(id, ',', (char**) idVector, idVecItems);

  //
  // ID-list and Type-list at the same time is not supported
  //
  if ((idVecItems > 1) && (typeVecItems > 1))
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "URI params /id/ and /type/ are both lists", "Not Permitted");
    return false;
  }

  //
  // Make sure all IDs are valid URIs
  //
  for (int ix = 0; ix < idVecItems; ix++)
  {
    if ((urlCheck(idVector[ix], &details) == false) && (urnCheck(idVector[ix], &details) == false))
    {
      LM_W(("Bad Input (Invalid Entity ID - Not a URL nor a URN)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Entity ID", "Not a URL nor a URN");
      return false;
    }
  }

  if (typeVecItems == 1)  // type needs to be modified according to @context
  {
    char* details;

    if (((strncmp(type, "http://", 7) == 0) || (strncmp(type, "https://", 8) == 0)) && (urlCheck(type, &details) == true))
    {
      // No expansion desired, the type is already a FQN
      strncpy(typeExpanded, type, sizeof(typeExpanded));
    }
    else if (orionldUriExpand(orionldState.contextP, type, typeExpanded, sizeof(typeExpanded), NULL, &details) == false)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Error during URI expansion of entity type", details);
      return false;
    }

    type          = typeExpanded;
    isTypePattern = false;  // Just in case ...
  }

  if (idVecItems > 1)  // A list of Entity IDs
  {
    for (int ix = 0; ix < idVecItems; ix++)
    {
      entityIdP = new EntityId(idVector[ix], type, "false", isTypePattern);
      parseData.qcr.res.entityIdVector.push_back(entityIdP);
    }
  }
  else if (typeVecItems > 1)  // A list of Entity Types
  {
    for (int ix = 0; ix < typeVecItems; ix++)
    {
      // FIXME: Check for FQN HERE TOO (once it is decided by ETSI)
      if (orionldUriExpand(orionldState.contextP, typeVector[ix], typeExpanded, sizeof(typeExpanded), NULL, &details) == false)
      {
        orionldErrorResponseCreate(OrionldBadRequestData, "Error during URI expansion of entity type", details);
        return false;
      }

      entityIdP = new EntityId(idString, typeExpanded, isIdPattern, false);
      parseData.qcr.res.entityIdVector.push_back(entityIdP);
    }
  }
  else  // Definitely no lists in EntityId id/type
  {
    entityIdP = new EntityId(idString, type, isIdPattern, isTypePattern);
    parseData.qcr.res.entityIdVector.push_back(entityIdP);
  }

  if (attrs != NULL)
  {
    char  longName[256];
    char* details;
    char* shortName;
    char* shortNameVector[32];
    int   vecItems = (int) sizeof(shortNameVector) / sizeof(shortNameVector[0]);

    vecItems = kStringSplit(attrs, ',', (char**) shortNameVector, vecItems);

    for (int ix = 0; ix < vecItems; ix++)
    {
      shortName = shortNameVector[ix];

      if (orionldUriExpand(orionldState.contextP, shortName, longName, sizeof(longName), NULL, &details) == true)
        parseData.qcr.res.attributeList.push_back(longName);
      else
      {
        orionldErrorResponseCreate(OrionldBadRequestData, "Error during URI expansion of attribute", shortName);
        parseData.qcr.res.release();
        return false;
      }
    }
  }

#if NGSILD_Q_FILTER
  if (q != NULL)
  {
    char* title;
    char* details;

    LM_TMP(("Q: got a Q-Filter: %s", q));

    QNode* lexList;
    QNode* qTree;

    if ((lexList = qLex(q, &title, &details)) == NULL)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, title, details);
      parseData.qcr.res.release();
      return false;
    }

    if ((qTree = qParse(lexList, &title, &details)) == NULL)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, title, details);
      parseData.qcr.res.release();
      return false;
    }

    orionldState.qMongoFilterP = new mongo::BSONObj;

    LM_TMP(("Q: Calling qTreeToBsonObj"));
    mongo::BSONObjBuilder objBuilder;
    if (qTreeToBsonObj(qTree, &objBuilder, &title, &details) == false)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, title, details);
      parseData.qcr.res.release();
      return false;
    }

#if 0
    LM_TMP(("Q: Setting qMongoFilterP: %s (DESTRUCTIVE!!!)", objBuilder.obj().toString().c_str()));
#else
    LM_TMP(("Q: Setting qMongoFilterP"));
#endif

    *orionldState.qMongoFilterP = objBuilder.obj();
  }

#else

  if (q != NULL)  // Old APIv2 StringFilter
  {
    //
    // ngsi-ld doesn't support metadata which orion APIv1 and v2 does.
    // However, for simplicity, the metadata vector is used for properties of an attribute.
    // As metadata "isn't supported", the mq filter isn't supported, but as attribute properties are stored as metadata,
    // we need to use the mq mechanism internally, if a property of a property is in the left hand side of a q filter item.
    //
    // So, the entire StringFilter is considered a MQ filter, but if the left hand side is just an attribute name (the metadata part is empty)
    // then we must turn the parsed filter into a Q filter instead.
    //
    // The problem with this approach us that we can't reach inside compound valus of a Property,
    // For example, consider the following entity:
    // {
    //   "id": "http:...",
    //   "type": "",
    //   "P1": {
    //     "type": "Property",
    //     "value": { "P2": 13 },
    //     "P2": {
    //       "type": "Property",
    //       "value": 12
    //     }
    //   },
    //   ...
    // }
    //
    // And this StringFilter;    ?q=P1.P2==13.
    //
    // Which "P2" is the 'P1.P2==13' referring to?
    // Well, as the string filter is always MQ filters if "complex" (left hand side contains a dot),
    // it refers to the Property P2, NOT the field P2 inside the compound value of P1.
    //
    // One way of fixing this problem (in orion the problem was fixed by separation q and mq filters, q operating on compound values and
    // mq oprating on metadata) is to add the "value" keyword as part of the left hand side:  "?q=P1.value.P2==13".
    // This filter would refer to the P2 of the compound value of P1.
    // The implementation would have to remove the "value" keyword and change from MQ to Q filter, but it would work.
    //
    // FIXME: Need to do this with each q-item. Not only the first one
    //
    LM_T(LmtStringFilter, ("Q: q == '%s'", q));

    char* qP = q;
    while (*qP != 0)
    {
      char c = *qP;

      if ((c == '=') || (c == '>') || (c == '<') || (c == '!') || (c == '~'))
        break;
      if (c == '.')
        break;
      if (c == '[')
        break;

      ++qP;
    }

    //
    // If qP points to a '.', then it's a "metadata path" and a change to MQ filter must be done.
    // If qP points to a '[', then it's a "compound path" and the '[]' must be removed
    //
    StringFilterType filterType = SftQ;

    if (*qP == '.')
    {
      LM_T(LmtStringFilter, ("Q: Found a DOT in the Q - changing to MQ: '%s'", qP));
      filterType = SftMq;
    }
    else if (*qP == '[')
    {
      qP = q;

      LM_T(LmtStringFilter, ("Q: Found a [ in the Q: '%s'", qP));
      //
      // Copy char by char, replacing '[' for '.' and ']' for nothing ...
      // ... until reaching the operator.
      // The operator and all that comes after is simply copied
      //
      char* toP = q;

      LM_T(LmtStringFilter, ("Q: Compound Q: '%s'", q));
      while (*qP != 0)
      {
        char c = *qP;

        if ((c == '=') || (c == '>') || (c == '<') || (c == '!') || (c == '~'))
        {
          // Just copy the rest of chars
          LM_T(LmtStringFilter, ("Q: Found operator - rest: %s", qP));
          while (*qP != 0)
          {
            *toP = *qP;
            ++toP;
            ++qP;
          }
          *toP = 0;
          LM_T(LmtStringFilter, ("Q: Transformation Done: %s", toP));
          break;
        }
        else if (c == '[')  // Replace [ for .
        {
          *toP = '.';
          ++toP;
          ++qP;
        }
        else if (c == ']')  // Skip ]
        {
          ++qP;
        }
        else
        {
          *toP = *qP;
          ++toP;
          ++qP;
        }
      }

      // The resulting string is shorter than the original string - DON'T FORGET to terminate !!!
      *qP = 0;
      LM_T(LmtStringFilter, ("Q: Compound Q Transformed: '%s'", q));
    }

    Scope*        scopeP = new Scope((filterType == SftMq)? SCOPE_TYPE_SIMPLE_QUERY_MD : SCOPE_TYPE_SIMPLE_QUERY, q);
    StringFilter* sfP    = new StringFilter(filterType);

    if (filterType == SftMq)
      scopeP->mdStringFilterP = sfP;
    else
      scopeP->stringFilterP = sfP;

    LM_T(LmtStringFilter, ("Q: Created %s StringFilter of q: '%s'", (filterType == SftMq)? "MQ" : "Q", q));

    std::string details;
    if (sfP->parse(q, &details) == false)
    {
      delete scopeP;
      delete sfP;

      parseData.qcr.res.release();
      orionldErrorResponseCreate(OrionldBadRequestData, "Error parsing q StringFilter", details.c_str());
      LM_E(("Error parsing q StringFilter"));
      return false;
    }

    parseData.qcr.res.restriction.scopeVector.push_back(scopeP);
  }
#endif

  // Call standard op postQueryContext
  std::vector<std::string>  compV;    // Not used but part of signature for postQueryContext
  std::string               answer = postQueryContext(ciP, 0, compV, &parseData);

  //
  // Transform QueryContextResponse to KJ-Tree
  //
  ciP->httpStatusCode       = SccOk;
  orionldState.responseTree = kjTreeFromQueryContextResponse(ciP, false, keyValues, &parseData.qcrs.res);

  return true;
}
