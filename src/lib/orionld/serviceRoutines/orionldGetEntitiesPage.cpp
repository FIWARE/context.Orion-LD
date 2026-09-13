/*
*
* Copyright 2023 FIWARE Foundation e.V.
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
#include "ktrace/kTrace.h"                                          // KTrace
#include "kalloc/kaAlloc.h"                                         // kaAlloc
#include "kjson/KjNode.h"                                           // KjNode
#include "kjson/kjBuilder.h"                                        // kjArray, ...
#include "kjson/kjLookup.h"                                         // kjLookup
#include "kjson/kjClone.h"                                          // kjClone
#include "kjson/kjChildCount.h"                                     // kjChildCount
}

#include "orionld/types/DistOp.h"                                   // DistOp
#include "orionld/types/QNode.h"                                    // QNode
#include "orionld/types/OrionldGeoInfo.h"                           // OrionldGeoInfo
#include "orionld/common/orionldState.h"                            // orionldState, entityMaps
#include "orionld/common/orionldError.h"                            // orionldError
#include "orionld/common/pick.h"                                    // pickForEntityArray
#include "orionld/common/traceLevels.h"                             // KTrace Levels
#include "orionld/apiModel/ntocEntity.h"                            // ntocEntity
#include "orionld/apiModel/ntosEntity.h"                            // ntosEntity
#include "orionld/distOp/distOpLookupByRegId.h"                     // distOpLookupByRegId
#include "orionld/distOp/distOpListDebug.h"                         // distOpListDebug
#include "orionld/distOp/distOpsSend.h"                             // distOpsSend2
#include "orionld/distOp/distOpItemListDebug.h"                     // distOpItemListDebug
#include "orionld/distOp/distOpListItemAdd.h"                       // distOpListItemAdd
#include "orionld/distOp/distOpResponseMergeIntoEntityArray.h"      // distOpResponseMergeIntoEntityArray
#include "orionld/distOp/distOpsSendAndReceive.h"                   // distOpsSendAndReceive
#include "orionld/context/orionldAttributeExpand.h"                 // orionldAttributeExpand
#include "orionld/context/orionldContextItemExpand.h"               // orionldContextItemExpand
#include "orionld/context/orionldSubAttributeExpand.h"              // orionldSubAttributeExpand
#include "orionld/common/geoCompile.h"                              // geoCompile
#include "orionld/common/geosInit.h"                                // geosHandle
#include "orionld/types/OrionldGeometry.h"                          // orionldGeometryToString
#include "orionld/q/qMatch.h"                                       // qMatch
#include "orionld/notifications/geoMatch.h"                         // geoMatch
#include "orionld/linkedEntities/eLinkRelationsRetrieve.h"          // eLinkRelationsRetrieve
#include "orionld/linkedEntities/eLinkInlineExpand.h"               // eLinkInlineExpand
#include "orionld/serviceRoutines/orionldGetEntitiesLocal.h"        // orionldGetEntitiesLocal
#include "orionld/serviceRoutines/orionldGetEntitiesPage.h"         // Own interface



// -----------------------------------------------------------------------------
//
// cleanupSysAttrs -
//
static void cleanupSysAttrs(void)
{
  KT_T(KtSysAttrs, "orionldState.responseTree: %p", orionldState.responseTree);

  for (KjNode* entityP = orionldState.responseTree->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    KjNode*     idP = kjLookup(entityP, "id");
    const char* id  = (idP != NULL)? idP->value.s : "unidentified";

    KT_T(KtSysAttrs, "Removing sysAttrs for the entity '%s'", id);

    KjNode* createdAtP  = kjLookup(entityP, "createdAt");
    KjNode* modifiedAtP = kjLookup(entityP, "modifiedAt");
    if (createdAtP  != NULL) kjChildRemove(entityP, createdAtP);
    if (modifiedAtP != NULL) kjChildRemove(entityP, modifiedAtP);

    for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      if (attrP->type != KjObject)
        continue;

      KT_T(KtSysAttrs, "Removing sysAttrs for the attribute '%s'", attrP->name);

      KjNode* createdAtP  = kjLookup(attrP, "createdAt");
      KjNode* modifiedAtP = kjLookup(attrP, "modifiedAt");
      if (createdAtP  != NULL) kjChildRemove(attrP, createdAtP);
      if (modifiedAtP != NULL) kjChildRemove(attrP, modifiedAtP);

      for (KjNode* subAttrP = attrP->value.firstChildP; subAttrP != NULL; subAttrP = subAttrP->next)
      {
        if (subAttrP->type != KjObject)
          continue;

        KT_T(KtSysAttrs, "Removing sysAttrs for the sub-attribute '%s'", subAttrP->name);

        KjNode* createdAtP  = kjLookup(subAttrP, "createdAt");
        KjNode* modifiedAtP = kjLookup(subAttrP, "modifiedAt");
        if (createdAtP  != NULL) kjChildRemove(subAttrP, createdAtP);
        if (modifiedAtP != NULL) kjChildRemove(subAttrP, modifiedAtP);
      }
    }
  }
}



// -----------------------------------------------------------------------------
//
// idListFix -
//
static void idListFix(KjNode* entityIdArray)
{
  int entityIds = kjChildCount(entityIdArray);

  orionldState.in.idList.items = entityIds;
  orionldState.in.idList.array = (char**) kaAlloc(&orionldState.kalloc, sizeof(char*) * entityIds);

  KjNode* entityIdP = entityIdArray->value.firstChildP;
  for (int ix = 0; ix < entityIds; ix++)
  {
    orionldState.in.idList.array[ix] = entityIdP->value.s;
    entityIdP = entityIdP->next;
  }
}



// -----------------------------------------------------------------------------
//
// queryResponse -
//
static int queryResponse(DistOp* distOpP, void* callbackParam)
{
  KjNode* entityArray = (KjNode*) callbackParam;

  distOpResponseMergeIntoEntityArray(distOpP, entityArray);
  return 0;
}




// -----------------------------------------------------------------------------
//
// formatFix -
//
static void formatFix(KjNode* entityArray, int skip)
{
  int ix = -1;
  for (KjNode* entityP = entityArray->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    ++ix;
    if (ix < skip)  // Entities from the local DB have already been transformed to the desired format
    {
      KT_T(KtFormat, "Skipping child %d as it comes from local DB", ix);
      continue;
    }

    KT_T(KtFormat, "Fixing format for child %d as it comes from remote", ix);

    if (orionldState.out.format == RF_CONCISE)
      ntocEntity(entityP, orionldState.uriParams.lang, orionldState.uriParamOptions.sysAttrs);
    else if (orionldState.out.format == RF_SIMPLIFIED)
      ntosEntity(entityP, orionldState.uriParams.lang);
  }
}



// -----------------------------------------------------------------------------
//
// entityNamesExpand - expand the Attribute names of an assembled Entity, in place
//
// The Entities of a page carry compacted Attribute names - the local ones were compacted on their way
// out of the database, the remote ones arrived that way.  A QNode's variable paths are expanded (see
// qVariableFix), and so is the geoProperty of a geo-filter (see pCheckGeo), and that is what qMatch
// and geoMatch look the Attribute up by.  So a clone of the Entity gets its names expanded - with the
// very same two functions qVariableFix uses, so that the two forms line up by construction.
//
// NOTE
//   Expanded, but NOT dot-for-eq'ed, even though qVariableFix does that to its paths: geoMatch looks
//   its geoProperty up with a plain kjLookup and would miss an '='-form name, while for 'q' the two
//   forms are equivalent - kjTreeNavigate tries the dot-form as a fallback.
//
static void entityNamesExpand(KjNode* entityP)
{
  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type != KjObject)  // 'id' and 'type' - and they're never expanded
      continue;

    attrP->name = orionldAttributeExpand(orionldState.contextP, attrP->name, true, NULL);

    for (KjNode* subAttrP = attrP->value.firstChildP; subAttrP != NULL; subAttrP = subAttrP->next)
    {
      //
      // A VocabProperty's value is a term, and it was compacted on its way out of the database
      // (dbModelToApiAttribute).  'q' on a VocabProperty compares against the EXPANDED value - that
      // is what 'expandValues' is for - so the value has to go back to what it was.
      //
      if (strcmp(subAttrP->name, "vocab") == 0)
      {
        if (subAttrP->type == KjString)
          subAttrP->value.s = orionldContextItemExpand(orionldState.contextP, subAttrP->value.s, true, NULL);
        else if (subAttrP->type == KjArray)
        {
          for (KjNode* wordP = subAttrP->value.firstChildP; wordP != NULL; wordP = wordP->next)
          {
            if (wordP->type == KjString)
              wordP->value.s = orionldContextItemExpand(orionldState.contextP, wordP->value.s, true, NULL);
          }
        }

        continue;
      }

      if ((strcmp(subAttrP->name, "type")   == 0) || (strcmp(subAttrP->name, "value") == 0) ||
          (strcmp(subAttrP->name, "object") == 0))
        continue;

      subAttrP->name = orionldSubAttributeExpand(orionldState.contextP, subAttrP->name, true, NULL);
    }
  }
}



// -----------------------------------------------------------------------------
//
// assembledEntitiesFilter - apply 'q' and the geo-filter to Entities that have been assembled
//
// If Entities may be split over several Context Sources, no filter can be pushed down - neither to a
// Context Source nor to the local database, as each of them only ever holds a part of the Entity, and
// a filter on a part is a filter on the wrong thing.  The Entity Map then holds candidate Entities,
// and the filtering happens here, once the Entity is whole.  TS 104-175 clause 10.4.3:
//   "These filters then have to be applied after the Entity information from different Context
//    Sources and local information, if there is any, has been aggregated"
//
// NOTE
//   The Entities are removed from the page, not from the Entity Map, so the count of a filtered
//   distributed query is the number of CANDIDATES.  Clause 9.6 foresees exactly that: with split
//   Entities the map starts out holding candidates and the filters are re-checked while paginating.
//
static void assembledEntitiesFilter(KjNode* entityArray, QNode* qNode, OrionldGeoInfo* geoInfoP)
{
  bool geoFiltering = (geoInfoP != NULL) && (geoInfoP->geometry != GeoNoGeometry);

  if ((qNode == NULL) && (geoFiltering == false))
    return;

  GEOSGeometry*               geosGeometry = NULL;
  const GEOSPreparedGeometry* geosPrepared = NULL;

  if (geoFiltering == true)
    geoCompile(orionldGeometryToString(geoInfoP->geometry), geoInfoP->coordinates, geoInfoP->georel, &geosGeometry, &geosPrepared, "geoQ");

  KjNode* entityP = entityArray->value.firstChildP;

  while (entityP != NULL)
  {
    KjNode* next        = entityP->next;
    KjNode* expandedP   = kjClone(orionldState.kjsonP, entityP);
    bool    match       = true;

    entityNamesExpand(expandedP);

    if (qNode != NULL)
      match = qMatch(qNode, expandedP, false);

    if ((match == true) && (geoFiltering == true))
      match = geoMatch(geoInfoP, geosGeometry, geosPrepared, "geoQ", expandedP);

    if (match == false)
    {
      KjNode* idP = kjLookup(entityP, "id");
      KT_T(KtEntityMap, "Assembled Entity '%s' does not match the filters - removed from the page", (idP != NULL)? idP->value.s : "unidentified");
      kjChildRemove(entityArray, entityP);
    }

    entityP = next;
  }

  if (geosPrepared != NULL)
    GEOSPreparedGeom_destroy_r(geosHandle, geosPrepared);
  if (geosGeometry != NULL)
    GEOSGeom_destroy_r(geosHandle, geosGeometry);
}



// ----------------------------------------------------------------------------
//
// orionldGetEntitiesPage -
//
bool orionldGetEntitiesPage(QNode* qNodeApi, OrionldGeoInfo* geoInfoP)
{
  uint32_t  offset      = orionldState.uriParams.offset;
  uint32_t  limit       = orionldState.uriParams.limit;
  KjNode*   entityArray = kjArray(orionldState.kjsonP, NULL);

  //
  // Are the filters applied here, on the assembled Entity, instead of being pushed down?
  //
  // If so, the Entities must still be NORMALIZED when the filter runs - 'q' navigates to
  // "<attribute>.value", and in the simplified format there is no "value" to navigate to, just the
  // value itself.  So the local half is fetched normalized too (the remote halves always are), the
  // filter runs, and the format conversion is done for the whole page afterwards.
  //
  bool geoFiltering = (geoInfoP != NULL) && (geoInfoP->geometry != GeoNoGeometry);
  bool postFilter   = (orionldState.uriParams.splitEntities == true) && ((qNodeApi != NULL) || (geoFiltering == true));

  OrionldRenderFormat savedFormat = orionldState.out.format;
  if (postFilter == true)
    orionldState.out.format = RF_NORMALIZED;

  KT_T(KtEntityMap, "entity map:          '%s'", orionldState.in.entityMap->id);
  KT_T(KtEntityMap, "items in entity map:  %d",  orionldState.in.entityMap->count);
  KT_T(KtEntityMap, "offset:               %d",  offset);
  KT_T(KtEntityMap, "limit:                %d",  limit);

  // HTTP Status code and payload body
  orionldState.responseTree   = entityArray;
  KT_T(KtSR, "orionldState.responseTree: %p", orionldState.responseTree);
  orionldState.httpStatusCode = 200;

  if (orionldState.uriParams.count == true)
  {
    KT_T(KtEntityMap, "%d entities match, in the entire federation", orionldState.in.entityMap->count);
    KT_T(KtEntityMap, "COUNT: Adding HttpResultsCount header: %d", orionldState.in.entityMap->count);
    orionldHeaderAdd(&orionldState.out.headers, HttpResultsCount, NULL, orionldState.in.entityMap->count);
  }

  if (offset >= orionldState.in.entityMap->count)
  {
    KT_T(KtEntityMap, "offset (%d) >= orionldState.in.entityMap->count (%d)", offset, orionldState.in.entityMap->count);
    return true;
  }

  //
  // Fast forward to offset index in the KJNode array that is the entity map
  //
  KjNode* entityMap = orionldState.in.entityMap->map->value.firstChildP;

  for (uint32_t ix = 0; ix < offset; ix++)
  {
    entityMap = entityMap->next;
  }

  //
  // entityMap now points to the first entity to give back.
  // Must extract all parts of the entities, according to their array inside the Entity Map,
  // and merge them together (in case of distributed entities
  //

  //
  // What we have here is a number of "slots" in the entity map, each slot with the layout:
  //
  // "urn:cp3:entities:E30" [ "urn:Reg1", "urn:Reg2", null ]
  //
  // To avoid making a DB query, of forwarded request, for each and every entity in the slots,
  // we must here group them into:
  //
  //   {
  //     "urn:reg1": [ "urn:E1", ... "urn:En" ],
  //     "urn:reg2": [ "urn:E1", ... "urn:En" ],
  //     "@none":    [ "urn:E1", ... "urn:En" ]
  //   }
  // ]
  //
  // Once that array is ready, we can send forwarded requests or query the local DB
  //
  KjNode* sources = kjObject(orionldState.kjsonP, NULL);

  for (uint32_t ix = 0; ix < limit; ix++)
  {
    if (entityMap == NULL)  // in case we have less than "limit"
      break;

    char* entityId = entityMap->name;

    for (KjNode* regP = entityMap->value.firstChildP; regP != NULL; regP = regP->next)
    {
      const char* regId    = (regP->type == KjString)? regP->value.s : "@none";
      KjNode*     regArray = kjLookup(sources, regId);

      if (regArray == NULL)
      {
        regArray = kjArray(orionldState.kjsonP, regId);
        kjChildAdd(sources, regArray);
      }

      KjNode* entityIdNodeP = kjString(orionldState.kjsonP, NULL, entityId);
      kjChildAdd(regArray, entityIdNodeP);
    }

    entityMap = entityMap->next;
  }

  DistOpListItem* distOpListItem = NULL;
  KT_T(KtEntityMap, "Sending the distOp requests for the entity map");
  for (KjNode* sourceP = sources->value.firstChildP; sourceP != NULL; sourceP = sourceP->next)
  {
    if (strcmp(sourceP->name, "@none") == 0)
    {
      DistOp* distOpP = distOpLookupByRegId(orionldState.distOpList, "@none");
      KT_T(KtEntityMap, "distOpP->entityMap == %s", (distOpP->entityMap == true)? "true" : "false");

      // Local query - set input params for orionldGetEntitiesLocal

      // We want the entire entity this time, not only the entity id

      // The type doesn't matter, we have a list of entity ids
      orionldState.in.typeList.items = 0;

#if 0
      KT_T(KtDistOpAttributes, "------------ Local DB Query -------------");
      KT_T(KtDistOpAttributes, "orionldState.uriParams.attrs:   %s", orionldState.uriParams.attrs);
      KT_T(KtDistOpAttributes, "orionldState.in.attrList.items: %d", orionldState.in.attrList.items);
      for (int ix = 0; ix < orionldState.in.attrList.items; ix++)
      {
        KT_T(KtDistOpAttributes, "orionldState.in.attrList.array[%d]: '%s'", ix, orionldState.in.attrList.array[ix]);
      }
      KT_T(KtDistOpAttributes, "distOpP->attrsParam:          %s", distOpP->attrsParam);
      KT_T(KtDistOpAttributes, "distOpP->attrList:            %p", distOpP->attrList);

      if (distOpP->attrList != NULL)
      {
        KT_T(KtDistOpAttributes, "distOpP->attrList->items: %d", distOpP->attrList->items);
        for (int ix = 0; ix < distOpP->attrList->items; ix++)
        {
          KT_T(KtDistOpAttributes, "distOpP->attrList->array[%d]: '%s'", ix, distOpP->attrList->array[ix]);
        }
      }
#endif

      // Set orionldState.in.geometryPropertyExpanded according to the entityMap creation request (that's modifying the response)
      orionldState.in.geometryPropertyExpanded = NULL;  // FIXME: fix

      // Set orionldState.in.idList according to the entities in the entityMap
      idListFix(sourceP);

      // Use orionldState.in.attrList and not distOpP->attrList for local requests

      // No paging here
      orionldState.uriParams.offset = 0;
      orionldState.uriParams.limit  = orionldState.in.idList.items;

      KT_T(KtEntityMap, "Query local database for %d entities", orionldState.in.idList.items);
      orionldGetEntitiesLocal(distOpP->typeList,
                              distOpP->idList,
                              &orionldState.in.attrList,
                              &orionldState.in.pickList,
                              NULL,
                              (orionldState.uriParams.splitEntities == true)? NULL : distOpP->qNode,
                              (orionldState.uriParams.splitEntities == true)? NULL : &distOpP->geoInfo,
                              distOpP->lang,
                              true,                        // sysAttrs needed, to help pick attributes in case more than one of the same
                              distOpP->geometryProperty,
                              true);

      // Response comes in orionldState.responseTree - move those to entityArray
      if ((orionldState.responseTree != NULL) && (orionldState.responseTree->value.firstChildP != NULL))
      {
        KT_T(KtDistOpResponseDetail, "Adding a 'distop-response-entity' to the entityArray");

        orionldState.responseTree->lastChild->next = entityArray->value.firstChildP;
        entityArray->value.firstChildP = orionldState.responseTree->value.firstChildP;
        if (entityArray->lastChild == NULL)
          entityArray->lastChild = orionldState.responseTree->lastChild;
      }
    }
    else
    {
      int idStringSize = 0;

      KT_T(KtEntityMap, "Query '%s' for:", sourceP->name);
      for (KjNode* entityNodeP = sourceP->value.firstChildP; entityNodeP != NULL; entityNodeP = entityNodeP->next)
      {
        KT_T(KtEntityMap, "  o %s", entityNodeP->value.s);
        idStringSize += strlen(entityNodeP->value.s) + 1;  // +1 for the comma
      }

      char* idString   = kaAlloc(&orionldState.kalloc, idStringSize);
      int   idStringIx = 0;

      bzero(idString, idStringSize);
      for (KjNode* entityNodeP = sourceP->value.firstChildP; entityNodeP != NULL; entityNodeP = entityNodeP->next)
      {
        strcpy(&idString[idStringIx], entityNodeP->value.s);
        idStringIx += strlen(entityNodeP->value.s);

        if (entityNodeP->next != NULL)  // No comma if last item
        {
          idString[idStringIx] = ',';
          idStringIx += 1;
        }
      }

      KT_T(KtEntityMap, "Query '%s' with entity ids=%s", sourceP->name, idString);
      distOpListItem = distOpListItemAdd(distOpListItem, sourceP->name, idString);
    }
  }

  if (distOpListItem != NULL)
  {
    int localKids = kjChildCount(entityArray);

    KT_T(KtFormat, "Number of children from local: %d (no format fix for those)", localKids);

    distOpItemListDebug(distOpListItem, "To Forward for GET /entities");
    distOpsSendAndReceive(distOpListItem, queryResponse, entityArray);

    if (postFilter == false)
      formatFix(entityArray, localKids);
  }

  //
  // The Entities are whole now - this is where the filters go, if they could not be pushed down.
  // And only now can the page be rendered in the format the client asked for.
  //
  if (postFilter == true)
  {
    assembledEntitiesFilter(entityArray, qNodeApi, geoInfoP);

    //
    // An empty Entity array gets no Link header - the same rule orionldGetEntitiesLocal applies.
    // Without this, filtering the page down to nothing would answer "[]" WITH a Link header, while
    // every other way of arriving at "[]" answers without one.
    //
    if (entityArray->value.firstChildP == NULL)
      orionldState.noLinkHeader = true;

    orionldState.out.format = savedFormat;
    formatFix(entityArray, 0);  // 0: every Entity of the page is normalized, the local ones included
  }

  orionldState.responseTree   = entityArray;
  orionldState.httpStatusCode = 200;

  //
  // Time to cleanup ...
  //
  // 1. Remove all timestamps, unless requested
  //
  if (orionldState.uriParamOptions.sysAttrs == false)
    cleanupSysAttrs();

  if (orionldState.in.pickList.items > 0)
    pickForEntityArray();


  //
  // If Linked Entities, call eLinkRelationsRetrieve
  //
  if ((orionldState.in.linkedEntities == true) && (orionldState.uriParams.joinLevel > 0))
  {
    // orionldState.responseTree must be saved as eLinkEntityRetrieve sets it to NULL before calling orionldGetEntity
    KjNode* responseTree = orionldState.responseTree;
    KT_T(StLinked, "---------------------- Linked Entities  ----------------------");

    // First, clone the entire array of entities into orionldState.eLinkEntityV
    orionldState.eLinkEntityV = kjClone(orionldState.kjsonP, responseTree);

    //
    // Get all entities in an array (?join=flat). If ?join=inline, the array is modified into an object
    // eLinkRelationsRetrieve calls orionldGetEntity and ythe joinLevel needs to be taken down by 1 for this to work
    //
    orionldState.uriParams.joinLevel -= 1;
    for (KjNode* entityP = responseTree->value.firstChildP; entityP != NULL; entityP = entityP->next)
    {
      KT_T(StLinked, "Retreiving related entities for entity %p", entityP);
      eLinkRelationsRetrieve(orionldState.eLinkEntityV, entityP, 0);
    }

    if (orionldState.in.flat == false)
    {
      for (KjNode* entityP = responseTree->value.firstChildP; entityP != NULL; entityP = entityP->next)
      {
        eLinkInlineExpand(entityP, 0);
      }
    }
    else
      responseTree = orionldState.eLinkEntityV;

    orionldState.responseTree = responseTree;
  }

  return true;
}
