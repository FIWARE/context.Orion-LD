/*
*
* Copyright 2019 Telefonica Investigacion y Desarrollo, S.A.U
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
* Author: Fermin Galan, Ken Zangelin
*/
#include <string>                                              // std::string
#include <vector>                                              // std::vector

#include "mongo/client/dbclient.h"                             // mongo::BSONObj

#include "logMsg/logMsg.h"
#include "logMsg/traceLevels.h"


#include "apiTypesV2/Registration.h"                             // ngsiv2::Registration
#include "rest/HttpStatusCode.h"                                 // HttpStatusCode
#include "common/statistics.h"                                   // TIME_STAT_MONGO_READ_WAIT_START
#include "mongoBackend/MongoGlobal.h"                            // getMongoConnection
#include "mongoBackend/connectionOperations.h"                   // collectionQuery
#include "mongoBackend/dbConstants.h"                            // REG_*
#include "mongoBackend/safeMongo.h"                              // moreSafe
#include "orionld/mongoBackend/mongoLdRegistrationAux.h"         // mongoSetLd*
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "mongoBackend/mongoRegistrationAux.h"                   // Own interface



/* ****************************************************************************
*
* mongoSetRegistrationId -
*/
void mongoSetRegistrationId(ngsiv2::Registration* regP, const mongo::BSONObj* rP)
{
  regP->id = getFieldF(rP, "_id").OID().toString();
}



/* ****************************************************************************
*
* mongoSetDescription -
*/
void mongoSetDescription(ngsiv2::Registration* regP, const mongo::BSONObj* rP)
{
  if (rP->hasField(REG_DESCRIPTION))
  {
    regP->description         = getStringFieldF(rP, REG_DESCRIPTION);
    regP->descriptionProvided = true;
  }
  else
  {
    regP->description         = "";
    regP->descriptionProvided = false;
  }
}



/* ****************************************************************************
*
* mongoSetProvider -
*/
void mongoSetProvider(ngsiv2::Registration* regP, const mongo::BSONObj* rP)
{
  regP->provider.http.url = (rP->hasField(REG_PROVIDING_APPLICATION))? getStringFieldF(rP, REG_PROVIDING_APPLICATION): "";

  //
  // FIXME #3106: for the moment supportedForwardingMode is hardwired (i.e. DB is not taken
  // into account for them).
  //
  regP->provider.supportedForwardingMode = ngsiv2::ForwardAll;

  std::string format = rP->hasField(REG_FORMAT)? getStringFieldF(rP, REG_FORMAT) : "JSON";
  if (format == "JSON")
  {
    regP->provider.legacyForwardingMode = true;
  }
  else
  {
    // FIXME #3068: to be implemented once we define NGSIv2 based forwarding
  }
}



/* ****************************************************************************
*
* mongoSetEntities -
*/
static void mongoSetEntities(ngsiv2::Registration* regP, const mongo::BSONObj* cr0P)
{
  std::vector<mongo::BSONElement>  dbEntityV  = getFieldF(cr0P, REG_ENTITIES).Array();
  bool                             typeGiven  = false;

  for (unsigned int ix = 0; ix < dbEntityV.size(); ++ix)
  {
    ngsiv2::EntID    entity;
    mongo::BSONObj   ce = dbEntityV[ix].embeddedObject();

    if (ce.hasField(REG_ENTITY_ISPATTERN))
    {
      std::string isPattern = getStringFieldF(&ce, REG_ENTITY_ISPATTERN);

      if (isPattern == "true")
      {
        entity.idPattern = getStringFieldF(&ce, REG_ENTITY_ID);
      }
      else
      {
        entity.id = getStringFieldF(&ce, REG_ENTITY_ID);
      }
    }
    else
    {
      entity.id = getStringFieldF(&ce, REG_ENTITY_ID);
    }

    if (ce.hasField(REG_ENTITY_ISTYPEPATTERN))
    {
      std::string isPattern = getStringFieldF(&ce, REG_ENTITY_ISTYPEPATTERN);

      if (isPattern == "true")
      {
        entity.typePattern = getStringFieldF(&ce, REG_ENTITY_TYPE);
      }
      else
      {
        typeGiven = true;
        entity.type = getStringFieldF(&ce, REG_ENTITY_TYPE);
      }
    }
    else
    {
      typeGiven = true;
      entity.type = getStringFieldF(&ce, REG_ENTITY_TYPE);

      if (orionldState.apiVersion == NGSI_LD_V1)
        entity.type = orionldContextItemAliasLookup(orionldState.contextP, entity.type.c_str(), NULL, NULL);
    }

    if ((typeGiven == true) && (entity.type != "") && (orionldState.apiVersion == NGSI_LD_V1))
      entity.type = orionldContextItemAliasLookup(orionldState.contextP, entity.type.c_str(), NULL, NULL);

    regP->dataProvided.entities.push_back(entity);
  }
}



/* ****************************************************************************
*
* mongoSetAttributes -
*/
static void mongoSetAttributes(ngsiv2::Registration* regP, const mongo::BSONObj* cr0P)
{
  std::vector<mongo::BSONElement> dbAttributeV = getFieldF(cr0P, REG_ATTRS).Array();

  for (unsigned int ix = 0; ix < dbAttributeV.size(); ++ix)
  {
    mongo::BSONObj  aobj     = dbAttributeV[ix].embeddedObject();
    std::string     attrName = getStringFieldF(&aobj, REG_ATTRS_NAME);

    if (attrName != "")
    {
      regP->dataProvided.attributes.push_back(attrName);
    }
  }
}



/* ****************************************************************************
*
* mongoSetDataProvided -
*
* Make sure there is only ONE "contextRegistration" in the vector
* If we have more than one, then the Registration is made in API V1 as this is not
* possible in V2 and we cannot respond to the request using the current implementation of V2.
* This function will be changed to work in a different way once issue #3044 is dealt with.
*/
bool mongoSetDataProvided(ngsiv2::Registration* regP, const mongo::BSONObj* rP, bool arrayAllowed)
{
  std::vector<mongo::BSONElement> crV = getFieldF(rP, REG_CONTEXT_REGISTRATION).Array();

  if (crV.size() > 1)
  {
    return false;
  }

  //
  // Extract the first (and only) CR from the contextRegistration vector
  //
  mongo::BSONObj cr0 = crV[0].embeddedObject();

  mongoSetEntities(regP, &cr0);
  mongoSetAttributes(regP, &cr0);
  mongoSetProvider(regP, &cr0);

#ifdef ORIONLD
  mongoSetLdPropertyV(regP, &cr0);
  mongoSetLdRelationshipV(regP, &cr0);
#endif

  return true;
}



/* ****************************************************************************
*
* mongoSetExpires -
*/
void mongoSetExpires(ngsiv2::Registration* regP, const mongo::BSONObj& r)
{
  regP->expires = (r.hasField(REG_EXPIRATION))? getNumberFieldAsDoubleF(&r, REG_EXPIRATION) : -1;
}



/* ****************************************************************************
*
* mongoSetStatus -
*/
void mongoSetStatus(ngsiv2::Registration* regP, const mongo::BSONObj* rP)
{
  regP->status = (rP->hasField(REG_STATUS))? getStringFieldF(rP, REG_STATUS): "";
}
