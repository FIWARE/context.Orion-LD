#ifndef SRC_LIB_ORIONLD_CONTEXT_ORIONLDCONTEXTLIST_H_
#define SRC_LIB_ORIONLD_CONTEXT_ORIONLDCONTEXTLIST_H_

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
#include "orionld/context/OrionldContext.h"



// ----------------------------------------------------------------------------
//
// orionldContextHead/Tail
//
extern OrionldContext* orionldContextHead;
extern OrionldContext* orionldContextTail;



// ----------------------------------------------------------------------------
//
// orionldContextListInit -
//
extern int orionldContextListInit(char** detailsP);



// ----------------------------------------------------------------------------
//
// orionldContextListSemTake -
//
extern void orionldContextListSemTake(const char* who);



// ----------------------------------------------------------------------------
//
// orionldContextListSemGive -
//
extern void orionldContextListSemGive(const char* who);

#endif  // SRC_LIB_ORIONLD_CONTEXT_ORIONLDCONTEXTLIST_H_
