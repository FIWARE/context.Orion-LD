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
#include "mongo/client/dbclient.h"                                // MongoDB C++ Client Legacy Driver

extern "C"
{
#include "kjson/KjNode.h"                                         // KjNode
#include "kjson/kjLookup.h"                                       // kjLookup
}

#include "logMsg/logMsg.h"                                        // LM_*
#include "logMsg/traceLevels.h"                                   // Lmt*

#include "orionld/common/orionldState.h"                          // orionldState
#include "orionld/common/tenantList.h"                            // tenantList

#include "mongoBackend/MongoGlobal.h"                             // getMongoConnection, releaseMongoConnection, ...
#include "orionld/db/dbGeoIndexLookup.h"                          // dbGeoIndexLookup
#include "orionld/mongoCppLegacy/mongoCppLegacyDataToKjTree.h"    // mongoCppLegacyDataToKjTree
#include "orionld/mongoCppLegacy/mongoCppLegacyGeoIndexCreate.h"  // mongoCppLegacyGeoIndexCreate
#include "orionld/mongoCppLegacy/mongoCppLegacyGeoIndexInit.h"    // Own interface



// -----------------------------------------------------------------------------
//
// mongoCppLegacyGeoIndexInit -
//
void mongoCppLegacyGeoIndexInit(void)
{
  //
  // Loop over all tenants
  //
  OrionldTenant* tenantP = &tenant0;  // tenant0->next == tenantList :)
  tenant0.next = tenantList;          // Better safe than sorry!

  tenantP = &tenant0;
  while (tenantP != NULL)
  {
    // Foreach ENTITY (only attrs)
    mongo::BSONObjBuilder  dbFields;

    dbFields.append("attrs", 1);
    dbFields.append("_id", 0);

    mongo::BSONObjBuilder                 filter;
    mongo::Query                          query(filter.obj());
    mongo::BSONObj                        fieldsToReturn = dbFields.obj();
    mongo::DBClientBase*                  connectionP    = getMongoConnection();
    std::auto_ptr<mongo::DBClientCursor>  cursorP        = connectionP->query(tenantP->entities, query, 0, 0, &fieldsToReturn);

    orionldState.jsonBuf = NULL;
    while (cursorP->more())
    {
      if (orionldState.jsonBuf != NULL)
      {
        // mongoCppLegacyDataToKjTree uses orionldState.jsonBuf for its output tree
        free(orionldState.jsonBuf);
        orionldState.jsonBuf = NULL;
      }

      mongo::BSONObj  bsonObj = cursorP->nextSafe();

      char*           title;
      char*           detail;
      KjNode*         kjTree  = mongoCppLegacyDataToKjTree(&bsonObj, false, &title, &detail);
      KjNode*         attrsP  = kjTree->value.firstChildP;

      if (attrsP == NULL)  //  Entity without attributes ?
        continue;

      // Foreach PROPERTY
      for (KjNode* attrP = attrsP->value.firstChildP; attrP != NULL; attrP = attrP->next)
      {
        KjNode* typeP = kjLookup(attrP, "type");

        if (typeP == NULL)
        {
          LM_E(("Database Error (attribute '%s' has no 'type' field)", attrP->name));
          continue;
        }

        if (typeP->type != KjString)
        {
          LM_E(("Database Error (attribute with a 'type' field that is not a string)"));
          continue;
        }

        if (strcmp(typeP->value.s, "GeoProperty") == 0)
        {
          if (dbGeoIndexLookup(tenantP->tenant, attrP->name) == NULL)
            mongoCppLegacyGeoIndexCreate(tenantP, attrP->name);
        }
      }
    }

    tenantP = tenantP->next;
  }
}
