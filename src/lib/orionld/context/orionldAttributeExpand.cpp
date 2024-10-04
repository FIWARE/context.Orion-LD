/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
#include <string.h>                                              // strcmp

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "orionld/context/orionldContextItemAlreadyExpanded.h"   // orionldContextItemAlreadyExpanded
#include "orionld/context/orionldContextItemExpand.h"            // orionldContextItemExpand
#include "orionld/context/orionldAttributeExpand.h"              // Own interface



// -----------------------------------------------------------------------------
//
// orionldAttributeExpand -
//
// This function expands unless:
//   - has already been expanded
//   - is a special attribute such as 'location'
//
char* orionldAttributeExpand
(
  OrionldContext*       contextP,
  const char*           shortName,
  bool                  useDefaultUrlIfNotFound,
  OrionldContextItem**  contextItemPP
)
{
  char* sName = (char*) shortName;  // just to avoid a warning for strcmp

  if      (strcmp(sName, "id")               == 0) return sName;
  else if (strcmp(sName, "@id")              == 0) return sName;
  else if (strcmp(sName, "type")             == 0) return sName;
  else if (strcmp(sName, "@type")            == 0) return sName;
  else if (strcmp(sName, "scope")            == 0) return sName;
  else if (strcmp(sName, "location")         == 0) return sName;
  else if (strcmp(sName, "createdAt")        == 0) return sName;
  else if (strcmp(sName, "modifiedAt")       == 0) return sName;
  else if (strcmp(sName, "observationSpace") == 0) return sName;
  else if (strcmp(sName, "operationSpace")   == 0) return sName;

#if 1
  // FIXME: 'observedAt' as an attribute is not a thing - special treatment only if sub-attribute
  //  The implementation needs modifications.
  //
  //  Wait ... what if the @context says it must be a String, a DateTime?
  //  The @context doesn't distinguish between attributesd and sub-attributes ...
  //  It will need to be a String wherever it is.
  //
  //  We should probably forbid an attribute to have the name 'observedAt'
  //
  else if (strcmp(sName, "observedAt") == 0)
  {
    orionldContextItemExpand(contextP, sName, false, contextItemPP);
    return sName;
  }
#endif

  if (orionldContextItemAlreadyExpanded(sName) == true)
    return sName;

  return orionldContextItemExpand(contextP, sName, useDefaultUrlIfNotFound, contextItemPP);
}
