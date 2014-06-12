// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014 LSST Corporation.
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
#include "xrdsvc/SsiService.h"

// System headers
#include <iostream>
#include <string>

// Third-party headers
#include "XProtocol/XProtocol.hh"
#include "XrdSsi/XrdSsiLogger.hh"

// Qserv headers
#include "wlog/WLogger.h"
#include "wpublish/ChunkInventory.h"
#include "xrdsvc/SsiSession.h"
#include "wconfig/Config.h"
#include "sql/SqlConnection.h"
#include "xrdfs/XrdName.h"

class XrdPosixCallBack; // Forward.


namespace {
using lsst::qserv::wlog::WLogger;

class XrdSsiPrinter : public WLogger::Printer {
public:
    /// Takes ownership of XrdSsiLogger instance.
    XrdSsiPrinter(XrdSsiLogger* log) : _ssiLog(log) {}

    virtual WLogger::Printer& operator()(char const* s) {
        std::cerr << "Qserv " << s << std::endl;
        _ssiLog->Msg("Qserv", s);
        return *this;
    }
    boost::shared_ptr<XrdSsiLogger> _ssiLog;
};

} // anonymous namespace

namespace lsst {
namespace qserv {
namespace xrdsvc {

boost::shared_ptr<sql::SqlConnection> makeSqlConnection() {
    boost::shared_ptr<sql::SqlConnection> conn;
    mysql::MySqlConfig sqlConfig = wconfig::getConfig().getSqlConfig();
    // FIXME: Use qsmaster privileges for now.
    sqlConfig.username = "qsmaster";
    sqlConfig.dbName = "";
    conn.reset(new sql::SqlConnection(sqlConfig, true));
    return conn;
}


SsiService::SsiService(XrdSsiLogger* log) {
    boost::shared_ptr<lsst::qserv::wlog::WLogger::Printer>
        p(new XrdSsiPrinter(log));
    _log.reset(new lsst::qserv::wlog::WLogger(p));    
    _log->info("SsiService starting..");
    std::cerr << "EEEEE" << "service starting.\n";
    _initExports();
}

SsiService::~SsiService() { 
    _log->info("SsiService dying.");
}

bool 
SsiService::Provision(XrdSsiService::Resource* r,
                      unsigned short timeOut) { // Step 2
    std::ostringstream os;
    os << "Got provision call where rName is:"
       << r->rName;
    _log->info(os.str()); // Check here.
    std::cerr << "notice me!\n";

    _session = new SsiSession(r->rName, 
                              _chunkInventory->newValidator(),
                              _log);
    r->ProvisionDone(_session); // Step 3
    // client-side ProvisionDone()
    return true;
}

void SsiService::_initExports() {
    xrdfs::XrdName x;
    boost::shared_ptr<sql::SqlConnection> conn = makeSqlConnection();
    assert(conn);
    _chunkInventory.reset(new wpublish::ChunkInventory(x.getName(), *_log, conn));
    std::ostringstream os;
    os << "Paths exported: ";
    _chunkInventory->dbgPrint(os);
    _log->info(os.str());
}


}}} // lsst::qserv::xrdsvc