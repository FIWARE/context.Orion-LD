#ifndef SRC_LIB_ORIONLD_DDS_NGSILDENTITY_H_
#define SRC_LIB_ORIONLD_DDS_NGSILDENTITY_H_

/*
*
* Copyright 2024 FIWARE Foundation e.V.
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
* Author: David Campo, Ken Zangelin
*/

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
//
#include <cstdint>
#include <array>
#include <bitset>
#include <map>
#include <string>
#include <vector>

#include "fastcdr/cdr/fixed_size_string.hpp"
#include "fastcdr/xcdr/external.hpp"
#include "fastcdr/xcdr/optional.hpp"


#define eProsima_user_DllExport
#define NGSILDENTITY_DllAPI

namespace eprosima {
namespace fastcdr {
class Cdr;
class CdrSizeCalculator;
}  // namespace fastcdr
}  // namespace eprosima



/*!
 * @brief This class represents the structure NgsildEntity defined by the user in the IDL file.
 * @ingroup NgsildEntity
 */
class NgsildEntity
{
 public:
  /*!
   * @brief Default constructor.
   */
  eProsima_user_DllExport NgsildEntity();

  /*!
   * @brief Default destructor.
   */
  eProsima_user_DllExport ~NgsildEntity();

  /*!
   * @brief Copy constructor.
   * @param x Reference to the object NgsildEntity that will be copied.
   */
  eProsima_user_DllExport NgsildEntity(const NgsildEntity& x);

  /*!
   * @brief Move constructor.
   * @param x Reference to the object NgsildEntity that will be copied.
   */
  eProsima_user_DllExport NgsildEntity(NgsildEntity&& x) noexcept;

  /*!
   * @brief Copy assignment.
   * @param x Reference to the object NgsildEntity that will be copied.
   */
  eProsima_user_DllExport NgsildEntity& operator =(const NgsildEntity& x);

  /*!
   * @brief Move assignment.
   * @param x Reference to the object NgsildEntity that will be copied.
   */
  eProsima_user_DllExport NgsildEntity& operator =(NgsildEntity&& x) noexcept;

  /*!
   * @brief Comparison operator.
   * @param x NgsildEntity object to compare.
   */
  eProsima_user_DllExport bool operator ==(const NgsildEntity& x) const;

  /*!
   * @brief Comparison operator.
   * @param x NgsildEntity object to compare.
   */
  eProsima_user_DllExport bool operator !=(const NgsildEntity& x) const;

  /*!
   * @brief This function copies the value in member id
   * @param _id New value to be copied in member id
   */
  eProsima_user_DllExport void id(const std::string& _id);

  /*!
   * @brief This function moves the value in member id
   * @param _id New value to be moved in member id
   */
  eProsima_user_DllExport void id(std::string&& _id);

  /*!
   * @brief This function returns a constant reference to member id
   * @return Constant reference to member id
   */
  eProsima_user_DllExport const std::string& id() const;

  /*!
   * @brief This function returns a reference to member id
   * @return Reference to member id
   */
  eProsima_user_DllExport std::string& id();

  /*!
   * @brief This function copies the value in member type
   * @param _type New value to be copied in member type
   */
  eProsima_user_DllExport void type(const std::string& _type);

  /*!
   * @brief This function moves the value in member type
   * @param _type New value to be moved in member type
   */
  eProsima_user_DllExport void type(std::string&& _type);

  /*!
   * @brief This function returns a constant reference to member type
   * @return Constant reference to member type
   */
  eProsima_user_DllExport const std::string& type() const;

  /*!
   * @brief This function returns a reference to member type
   * @return Reference to member type
   */
  eProsima_user_DllExport std::string& type();

  /*!
   * @brief This function copies the value in member scope
   * @param _scope New value to be copied in member scope
   */
  eProsima_user_DllExport void scope(const std::string& _scope);

  /*!
   * @brief This function moves the value in member scope
   * @param _scope New value to be moved in member scope
   */
  eProsima_user_DllExport void scope(std::string&& _scope);

  /*!
   * @brief This function returns a constant reference to member scope
   * @return Constant reference to member scope
   */
  eProsima_user_DllExport const std::string& scope() const;

  /*!
   * @brief This function returns a reference to member scope
   * @return Reference to member scope
   */
  eProsima_user_DllExport std::string& scope();

  /*!
   * @brief This function sets a value in member createdAt
   * @param _createdAt New value for member createdAt
   */
  eProsima_user_DllExport void createdAt(uint64_t _createdAt);

  /*!
   * @brief This function returns the value of member createdAt
   * @return Value of member createdAt
   */
  eProsima_user_DllExport uint64_t createdAt() const;

  /*!
   * @brief This function returns a reference to member createdAt
   * @return Reference to member createdAt
   */
  eProsima_user_DllExport uint64_t& createdAt();

  /*!
   * @brief This function sets a value in member modifiedAt
   * @param _modifiedAt New value for member modifiedAt
   */
  eProsima_user_DllExport void modifiedAt(uint64_t _modifiedAt);

  /*!
   * @brief This function returns the value of member modifiedAt
   * @return Value of member modifiedAt
   */
  eProsima_user_DllExport uint64_t modifiedAt() const;

  /*!
   * @brief This function returns a reference to member modifiedAt
   * @return Reference to member modifiedAt
   */
  eProsima_user_DllExport uint64_t& modifiedAt();

  /*!
   * @brief This function copies the value in member tenant
   * @param _tenant New value to be copied in member tenant
   */
  eProsima_user_DllExport void tenant(const std::string& _tenant);

  /*!
   * @brief This function moves the value in member tenant
   * @param _tenant New value to be moved in member tenant
   */
  eProsima_user_DllExport void tenant(std::string&& _tenant);

  /*!
   * @brief This function returns a constant reference to member tenant
   * @return Constant reference to member tenant
   */
  eProsima_user_DllExport const std::string& tenant() const;

  /*!
   * @brief This function returns a reference to member tenant
   * @return Reference to member tenant
   */
  eProsima_user_DllExport std::string& tenant();

  /*!
   * @brief This function copies the value in member attributes
   * @param _attributes New value to be copied in member attributes
   */
  eProsima_user_DllExport void attributes(const std::string& _attributes);

  /*!
   * @brief This function moves the value in member attributes
   * @param _attributes New value to be moved in member attributes
   */
  eProsima_user_DllExport void attributes(std::string&& _attributes);

  /*!
   * @brief This function returns a constant reference to member attributes
   * @return Constant reference to member attributes
   */
  eProsima_user_DllExport const std::string& attributes() const;

  /*!
   * @brief This function returns a reference to member attributes
   * @return Reference to member attributes
   */
  eProsima_user_DllExport std::string& attributes();

 private:
    std::string m_id;
    std::string m_type;
    std::string m_scope;
    uint64_t    m_createdAt;
    uint64_t    m_modifiedAt;
    std::string m_tenant;
    std::string m_attributes;
};

#endif  // SRC_LIB_ORIONLD_DDS_NGSILDENTITY_H_
