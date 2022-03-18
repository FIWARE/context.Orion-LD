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
* Author: Fermin Galan Marquez
*
* Completely Rewritten by Ken Zangelin in June 2021
*/
#include <string>
#include <vector>

#include "logMsg/logMsg.h"
#include "ngsi/SubscriptionId.h"
#include "ngsi/RegistrationId.h"
#include "ngsi/StatusCode.h"

#include "orionld/mongoBackend/mongoTypeName.h"

#include "mongoBackend/safeMongo.h"



/* ****************************************************************************
*
* USING
*/
using mongo::BSONObj;
using mongo::BSONArray;
using mongo::BSONElement;
using mongo::DBClientCursor;
using mongo::OID;
using mongo::AssertionException;



/* ****************************************************************************
*
* getObjectField -
*/
bool getObjectField(BSONObj* outObjectP, const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);  // wish BSONObj::getField would take a BSONObj pointer instead of returning a copy on the stack ... :(

    type = element.type();
    if (type == mongo::Object)
    {
      *outObjectP = element.embeddedObject();
      return true;
    }
  }
  else
  {
    LM_E(("Runtime Error (object field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field, bP->toString().c_str(), caller, line));
    return false;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be an OBJECT but type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));

  return false;
}



/* ****************************************************************************
*
* getArrayField -
*/
bool getArrayField(BSONArray* outArrayP, const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);    // wish BSONObj::getField would take a BSONObj pointer instead of returning a copy on the stack ... :(

    type = element.type();
    if (type == mongo::Array)
    {
      // See http://stackoverflow.com/questions/36307126/getting-bsonarray-from-bsonelement-in-an-direct-way
      *outArrayP = (BSONArray) element.embeddedObject();
      return true;
    }
  }
  else
  {
    LM_E(("Runtime Error (array field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field, bP->toString().c_str(), caller, line));
    return false;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be an ARRAY but type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));

  return false;
}



/* ****************************************************************************
*
* getStringField -
*/
const char* getStringField(const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);    // wish BSONObj::getField would take a BSONObj pointer instead of returning a copy on the stack ... :(

    type = element.type();
    if (type == mongo::String)
      return element.valuestr();
  }
  else
  {
    LM_E(("Runtime Error (string field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field,
          bP->toString().c_str(),
          caller,
          line));
    return "";
  }

  LM_E(("Runtime Error (field '%s' was supposed to be a STRING but type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));

  return "";
}



/* ****************************************************************************
*
* getNumberField -
*/
double getNumberField(const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);

    type = element.type();
    if (type == mongo::NumberDouble)
      return element.Number();
  }
  else
  {
    LM_E(("Runtime Error (double field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field, bP->toString().c_str(), caller, line));
    return -1;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be a NUMBERDOUBLE but type=%d in BSONObj <%s> from caller %s:%d)",
        field, bP->getField(field).type(), bP->toString().c_str(), caller, line));

  return -1;
}



/* ****************************************************************************
*
* getIntField -
*/
int getIntField(const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);

    type = element.type();
    if (type == mongo::NumberInt)
      return element.numberInt();
  }
  else
  {
    LM_E(("Runtime Error (NumberInt field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field, bP->toString().c_str(), caller, line));
    return -1;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be a NUMBERINT but type=%d in BSONObj <%s> from caller %s:%d)",
        field, bP->getField(field).type(), bP->toString().c_str(), caller, line));

  return -1;
}



/* ****************************************************************************
*
* getIntOrLongFieldAsLong -
*/
long long getIntOrLongFieldAsLong(const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);

    type = element.type();

    if      (type == mongo::NumberLong)      return element.Long();
    else if (type == mongo::NumberInt)       return element.Int();
    else if (type == mongo::NumberDouble)    return element.Double();
  }
  else
  {
    LM_E(("Runtime Error (int/long field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field, bP->toString().c_str(), caller, line));
    return -1;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be int or long but type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));

  return -1;
}



/* ****************************************************************************
*
* getBoolField -
*/
bool getBoolField(const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);

    type = element.type();

    if (type == mongo::Bool)
      return element.Bool();
  }
  else
  {
    LM_E(("Runtime Error (bool field '%s' is missing in BSONObj <%s>)", field, bP->toString().c_str()));
    return false;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be a bool but type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));

  return false;
}



/* ****************************************************************************
*
* getNumberFieldAsDouble -
*/
double getNumberFieldAsDouble(const BSONObj* bP, const char* field, const char* caller, int line)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);

    type = element.type();

    if      (type == mongo::NumberDouble)      return element.Double();
    else if (type == mongo::NumberLong)        return element.Long();
    else if (type == mongo::NumberInt)         return element.Int();
  }
  else
  {
    LM_E(("Runtime Error (double/int/long field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field,
          bP->toString().c_str(),
          caller,
          line));

    return -1;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be a Number (double/int/long) but the type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));

  return -1;
}



/* ****************************************************************************
*
* getField -
*/
BSONElement getField(const BSONObj* bP, const char* field, const char* caller, int line)
{
  if (bP->hasField(field))
    return bP->getField(field);

  LM_E(("Runtime Error (field '%s' is missing in BSONObj <%s> from caller %s:%d)",
        field,
        bP->toString().c_str(),
        caller,
        line));

  return BSONElement();
}



/* ****************************************************************************
*
* setStringVector -
*/
void setStringVector
(
  const BSONObj*             bP,
  const char*                field,
  std::vector<std::string>*  v,
  const char*                caller,
  int                        line
)
{
  mongo::BSONType type = mongo::EOO;

  if (bP->hasField(field))
  {
    BSONElement element = bP->getField(field);

    type = element.type();
    if (type == mongo::Array)
    {
      // See http://stackoverflow.com/questions/36307126/getting-bsonarray-from-bsonelement-in-an-direct-way
      std::vector<BSONElement> ba = bP->getField(field).Array();

      v->clear();

      for (unsigned int ix = 0; ix < ba.size(); ++ix)
      {
        if (ba[ix].type() == mongo::String)
        {
          v->push_back(ba[ix].String());
        }
        else
        {
          LM_E(("Runtime Error (element %d in array was supposed to be a STRING but type=%d from caller %s:%d)",
                ix, ba[ix].type(), caller, line));
          v->clear();

          return;  // Error reported in logfile ...
        }
      }

      return;  // All GOOD
    }
  }
  else
  {
    LM_E(("Runtime Error (object field '%s' is missing in BSONObj <%s> from caller %s:%d)",
          field, bP->toString().c_str(), caller, line));

    return;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be an array but type=%d in BSONObj <%s> from caller %s:%d)",
        field, type, bP->toString().c_str(), caller, line));
}



/* ****************************************************************************
*
* moreSafe -
*
* This is a safe version of the more() method for cursors in order to avoid
* exception that may crash the broker. However, if the more() method is returning
* an exception something really bad is happening, it is considered a Fatal Error.
*
* (The name resembles the same relationship between next() and nextSafe() in the mongo driver)
*/
bool moreSafe(const std::auto_ptr<DBClientCursor>& cursor)
{
  try
  {
    return cursor->more();
  }
  catch (const std::exception& e)
  {
    LM_E(("Fatal Error (more() exception: %s)", e.what()));
    return false;
  }
  catch (...)
  {
    LM_E(("Fatal Error (more() exception: generic)"));
    return false;
  }
}



/* ****************************************************************************
*
* nextSafeOrError -
*/
bool nextSafeOrError
(
  const std::auto_ptr<DBClientCursor>&  cursor,
  BSONObj*                              r,
  std::string*                          err,
  const char*                           caller,
  int                                   line
)
{
  try
  {
    *r = cursor->nextSafe();
    return true;
  }
  catch (const std::exception &e)
  {
    char lineString[STRING_SIZE_FOR_INT];

    snprintf(lineString, sizeof(lineString), "%d", line);
    *err = std::string(e.what()) + " at " + caller + ":" + lineString;

    return false;
  }
  catch (...)
  {
    char lineString[STRING_SIZE_FOR_INT];

    snprintf(lineString, sizeof(lineString), "%d", line);
    *err = std::string("generic exception at ") + caller + ":" + lineString;

    return false;
  }
}



/* ****************************************************************************
*
* safeGetSubId -
*/
bool safeGetSubId(const SubscriptionId* subIdP, OID* id, StatusCode* sc)
{
  try
  {
    *id = OID(subIdP->get());
    return true;
  }
  catch (const AssertionException &e)
  {
    // FIXME P3: this check is "defensive", but from a efficiency perspective this should be short-cut at
    // parsing stage. To check it. The check should be for: [0-9a-fA-F]{24}.
    sc->fill(SccContextElementNotFound);
    return false;
  }
  catch (const std::exception &e)
  {
    sc->fill(SccReceiverInternalError, e.what());
    return false;
  }
  catch (...)
  {
    sc->fill(SccReceiverInternalError, "generic exception");
    return false;
  }
}



/* ****************************************************************************
*
* safeGetRegId -
*/
bool safeGetRegId(const char* regId, OID* id, StatusCode* sc)
{
  try
  {
    *id = OID(regId);
    return true;
  }
  catch (const AssertionException &e)
  {
    //
    // FIXME P3: this check is "defensive", but from a efficiency perspective this should be short-cut at
    // parsing stage. To check it. The check should be for: [0-9a-fA-F]{24}.
    //
    sc->fill(SccContextElementNotFound);
    return false;
  }
  catch (const std::exception &e)
  {
    sc->fill(SccReceiverInternalError, e.what());
    return false;
  }
  catch (...)
  {
    sc->fill(SccReceiverInternalError, "generic exception");
    return false;
  }
}
