/*
 * LSST Data Management System
 * Copyright 2015 AURA/LSST.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <https://www.lsstcorp.org/LegalNotices/>.
 */
#ifndef LSST_QSERV_CCONTROL_USERQUERYTYPE_H
#define LSST_QSERV_CCONTROL_USERQUERYTYPE_H

// System headers
#include <string>

// Third-party headers

// Qserv headers


namespace lsst {
namespace qserv {
namespace ccontrol {

/// @addtogroup ccontrol

/**
 *  @ingroup ccontrol
 *
 *  @brief Helper class for parsing queries and determining their types.
 */

class UserQueryType  {
public:

    /// Returns true if query is DROP DATABASE
    static bool isDropDb(std::string const& query, std::string& dbName);

    /// Returns true if query is DROP TABLE
    static bool isDropTable(std::string const& query, std::string& dbName, std::string& tableName);

    /// Returns true if query is SELECT
    static bool isSelect(std::string const& query);

    /// Returns true if query is FLUSH QSERV_CHUNKS_CACHE [FOR database]
    static bool isFlushChunksCache(std::string const& query, std::string& dbName);

};

}}} // namespace lsst::qserv::ccontrol

#endif // LSST_QSERV_CCONTROL_USERQUERYTYPE_H
