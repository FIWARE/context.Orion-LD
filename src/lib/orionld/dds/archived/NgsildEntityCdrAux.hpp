//
// Copyright 2024 FIWARE Foundation e.V.
//
// This file is part of Orion-LD Context Broker.
//
// Orion-LD Context Broker is free software: you can redistribute it and/or
// modify it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// Orion-LD Context Broker is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
// General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
//
// For those usages not covered by this license please contact with
// orionld at fiware dot org
//

// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*!
 * @file NgsildEntityCdrAux.hpp
 * This source file contains some definitions of CDR related functions.
 *
 * This file was generated by the tool fastddsgen.
 */

#ifndef _FAST_DDS_GENERATED_NGSILDENTITYCDRAUX_HPP_
#define _FAST_DDS_GENERATED_NGSILDENTITYCDRAUX_HPP_

#include "NgsildEntity.h"

constexpr uint32_t NgsildEntity_max_cdr_typesize {1320UL};
constexpr uint32_t NgsildEntity_max_key_cdr_typesize {0UL};


namespace eprosima {
namespace fastcdr {

class Cdr;
class CdrSizeCalculator;



eProsima_user_DllExport void serialize_key(
        eprosima::fastcdr::Cdr& scdr,
        const NgsildEntity& data);


} // namespace fastcdr
} // namespace eprosima

#endif // _FAST_DDS_GENERATED_NGSILDENTITYCDRAUX_HPP_

