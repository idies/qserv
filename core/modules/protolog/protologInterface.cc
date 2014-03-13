/*
 * LSST Data Management System
 * Copyright 2013 LSST Corporation.
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
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
// protologInterface.cc houses the implementation of
// protologInterface.h (SWIG-exported functions for writing to protolog.)

#include "protolog/ProtoLog.h"
#include "protolog/protologInterface.h"
#include <stdarg.h>

namespace qMaster=lsst::qserv::master;

void qMaster::initLog_iface() {
    lsst::qserv::ProtoLog::initLog();
}

void qMaster::log_iface(std::string const& filename,
                        std::string const& funcname,
                        int lineno,
                        std::string const& loggername,
                        int level,
                        std::string const& fmt,
                        ...) {
    va_list args;
    va_start(args, fmt);
    lsst::qserv::ProtoLog::vlog(filename, funcname, lineno, loggername,
        static_cast<lsst::qserv::ProtoLog::log_level>(level), fmt, args);
}


