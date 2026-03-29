/*
*
* Copyright 2013 Telefonica Investigacion y Desarrollo, S.A.U
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
#include <stdio.h>
#include <string>

extern "C"
{
#include "ktrace/kTrace.h"
}

#include "common/globals.h"
#include "common/tag.h"
#include "alarmMgr/alarmMgr.h"
#include "ngsi/ContextElement.h"
#include "ngsi/ContextAttribute.h"
#include "ngsi10/UpdateContextRequest.h"
#include "ngsi10/UpdateContextResponse.h"


/* ****************************************************************************
*
* UpdateContextRequest::UpdateContextRequest -
*/
UpdateContextRequest::UpdateContextRequest()
{
  updateActionType = ActionTypeUpdate;
}



/* ****************************************************************************
*
* UpdateContextRequest::UpdateContextRequest -
*/
UpdateContextRequest::UpdateContextRequest(const std::string& _contextProvider, EntityId* eP)
{
  contextProvider = _contextProvider;
  contextElementVector.push_back(new ContextElement(eP));
  updateActionType = ActionTypeUpdate;
}



/* ****************************************************************************
*
* UpdateContextRequest::render -
*/
std::string UpdateContextRequest::render(ApiVersion apiVersion, bool asJsonObject)
{
  std::string  out = "";

  // JSON commas:
  // Both fields are MANDATORY, so, comma after "contextElementVector"
  //  
  out += startTag();
  out += contextElementVector.render(apiVersion, asJsonObject, UpdateContext, true);
  out += valueTag("updateAction", actionTypeString(apiVersion, updateActionType), false);
  out += endTag(false);

  return out;
}


/* ****************************************************************************
*
* UpdateContextRequest::check -
*/
std::string UpdateContextRequest::check(ApiVersion apiVersion, bool asJsonObject, const std::string& predetectedError)
{
  std::string            res;
  UpdateContextResponse  response;

  if (predetectedError != "")
  {
    response.errorCode.fill(SccBadRequest, predetectedError);
    return response.render(apiVersion, asJsonObject);
  }

  if ((res = contextElementVector.check(apiVersion, UpdateContext)) != "OK")
  {
    response.errorCode.fill(SccBadRequest, res);
    return response.render(apiVersion, asJsonObject);
  }

  return "OK";
}



/* ****************************************************************************
*
* UpdateContextRequest::release -
*/
void UpdateContextRequest::release(void)
{
  contextElementVector.release();
}



/* ****************************************************************************
*
* UpdateContextRequest::fill -
*/
void UpdateContextRequest::fill(const Entity* entP, ActionType _updateActionType)
{
  ContextElement*  ceP = new ContextElement(entP->id, entP->type, "false");

  ceP->contextAttributeVector.fill((ContextAttributeVector*) &entP->attributeVector);

  contextElementVector.push_back(ceP);
  updateActionType = _updateActionType;
}



/* ****************************************************************************
*
* UpdateContextRequest::fill -
*/
void UpdateContextRequest::fill
(
  const std::string&   entityId,
  ContextAttribute*    attributeP,
  ActionType           _updateActionType,
  const std::string&   type
)
{
  ContextElement*   ceP = new ContextElement(entityId, type, "false");
  ContextAttribute* aP  = new ContextAttribute(attributeP);

  ceP->contextAttributeVector.push_back(aP);
  contextElementVector.push_back(ceP);
  updateActionType = _updateActionType;
}



/* ****************************************************************************
*
* UpdateContextRequest::fill -
*
* Instead of copying the attributes, the created ContextElements will just point to
* the already existing ContextAttributes and the original vector is then cleared to
* avoid any double-free problems.
*/
void UpdateContextRequest::fill
(
  Entities*    entities,
  ActionType  _updateActionType
)
{
  updateActionType = _updateActionType;

  for (unsigned int eIx = 0; eIx < entities->vec.size(); ++eIx)
  {
    Entity*           eP  = entities->vec[eIx];
    ContextElement*   ceP = new ContextElement(eP->id, eP->type, eP->isPattern);

    for (unsigned int aIx = 0; aIx < eP->attributeVector.size(); ++aIx)
    {
      // NOT copying the attribute, just pointing to it - original vector is then cleared
      ceP->contextAttributeVector.push_back(eP->attributeVector[aIx]);
    }

    eP->attributeVector.vec.clear();  // original vector is cleared
    contextElementVector.push_back(ceP);
  }
}



/* ****************************************************************************
*
* UpdateContextRequest::attributeLookup -
*/
ContextAttribute* UpdateContextRequest::attributeLookup(EntityId* eP, const std::string& attributeName)
{
  for (unsigned int ceIx = 0; ceIx < contextElementVector.size(); ++ceIx)
  {
    EntityId* enP = &contextElementVector[ceIx]->entityId;

    if ((enP->id != eP->id) || (enP->type != eP->type))
    {
      continue;
    }

    ContextElement* ceP = contextElementVector[ceIx];

    for (unsigned int aIx = 0; aIx < ceP->contextAttributeVector.size(); ++aIx)
    {
      ContextAttribute* aP = ceP->contextAttributeVector[aIx];

      if (aP->name == attributeName)
      {
        return aP;
      }
    }
  }

  return NULL;
}
