/*
*
* Copyright 2026 FIWARE Foundation e.V.
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
#include <stdlib.h>                                              // free
#include <regex.h>                                               // regfree
#include <geos_c.h>                                              // GEOSGeom_destroy_r, GEOSPreparedGeom_destroy_r

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjFree.h"                                        // kjFree
}

#include "orionld/types/SubCacheItem.h"                          // SubCacheItem
#include "orionld/types/SubV2Info.h"                             // SubV2Info                          // SubCacheItem
#include "orionld/types/SubEntitySelector.h"                     // SubEntitySelector
#include "orionld/common/geosInit.h"                             // geosHandle
#include "orionld/q/qRelease.h"                                  // qRelease
#include "orionld/subCache/subCacheItemRelease.h"                // Own interface



// -----------------------------------------------------------------------------
//
// subCacheItemCompiledStateRelease -
//
void subCacheItemCompiledStateRelease(SubCacheItem* sciP)
{
  //
  // The entity selectors - their id/idPattern/type all point into subTree, only the
  // compiled regex and the selector itself are owned here.
  //
  SubEntitySelector* sesP = sciP->entitySelectors;

  while (sesP != NULL)
  {
    SubEntitySelector* next = sesP->next;

    if (sesP->idRegexP != NULL)
      regfree(sesP->idRegexP);

    if (sesP->typeRegexP != NULL)
      regfree(sesP->typeRegexP);

    free(sesP);
    sesP = next;
  }
  sciP->entitySelectors = NULL;

  // 'qText' points into subTree - only the parsed tree is owned here
  if (sciP->qP != NULL)
  {
    qRelease(sciP->qP);
    sciP->qP = NULL;
  }
  sciP->qText = NULL;

  //
  // The geo state. 'geoQP' owns every KjNode 'geoInfo' points at (subCacheItemGeoCompile
  // makes sure of that, also for coordinates that arrived as a String), so geoInfo itself
  // only owns its strdup'd geoProperty.
  //
  if (geosHandle != NULL)
  {
    if (sciP->geosPrepared != NULL)
      GEOSPreparedGeom_destroy_r(geosHandle, sciP->geosPrepared);

    if (sciP->geosGeometry != NULL)
      GEOSGeom_destroy_r(geosHandle, sciP->geosGeometry);
  }
  sciP->geosPrepared = NULL;
  sciP->geosGeometry = NULL;

  if (sciP->geoInfo != NULL)
  {
    if (sciP->geoInfo->geoProperty != NULL)
      free(sciP->geoInfo->geoProperty);

    free(sciP->geoInfo);
    sciP->geoInfo = NULL;
  }

  if (sciP->geoQP != NULL)
  {
    kjFree(sciP->geoQP);
    sciP->geoQP = NULL;
  }

  // 'protocolString', 'ip' and 'rest' all point inside 'url' (or at a literal)
  if (sciP->url != NULL)
  {
    free(sciP->url);
    sciP->url = NULL;
  }
  sciP->protocolString = NULL;
  sciP->ip             = NULL;
  sciP->rest           = NULL;

  if (sciP->mqttP != NULL)
  {
    free(sciP->mqttP);
    sciP->mqttP = NULL;
  }

  //
  // The NGSIv2 state - C++, so 'delete', not 'free'
  //
  if (sciP->v2P != NULL)
  {
    delete sciP->v2P;
    sciP->v2P = NULL;
  }

  // 'lang' points into subTree
  sciP->lang = NULL;
}



// -----------------------------------------------------------------------------
//
// subCacheItemRelease -
//
void subCacheItemRelease(SubCacheItem* sciP)
{
  subCacheItemCompiledStateRelease(sciP);

  if (sciP->subId != NULL)
    free(sciP->subId);

  if (sciP->hostAlias != NULL)
    free(sciP->hostAlias);

  if (sciP->subTree != NULL)
    kjFree(sciP->subTree);

  free(sciP);
}
