// -*- LSST-C++ -*-

/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
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
// See dispatcher.h 

#include "lsst/qserv/master/xrdfile.h"
#include "lsst/qserv/master/dispatcher.h"
#include "lsst/qserv/master/thread.h"
#include "lsst/qserv/master/xrootd.h"
#include "lsst/qserv/master/SessionManager.h"
#include "lsst/qserv/master/AsyncQueryManager.h"
#include "lsst/qserv/master/QuerySession.h"
#include "lsst/qserv/QservPath.hh"

namespace qMaster = lsst::qserv::master;

using lsst::qserv::master::SessionManager;
typedef SessionManager<qMaster::AsyncQueryManager::Ptr> SessionMgr;
typedef boost::shared_ptr<SessionMgr> SessionMgrPtr;
namespace {

    SessionMgr& getSessionManager() {
        // Singleton for now.
        static SessionMgrPtr sm;
        if(sm.get() == NULL) {
            sm = boost::make_shared<SessionMgr>();
        }
        assert(sm.get() != NULL);
        return *sm;
    }

    // Deprecated
    qMaster::QueryManager& getManager(int session) {
        // Singleton for now. //
        static boost::shared_ptr<qMaster::QueryManager> qm;
        if(qm.get() == NULL) {
            qm = boost::make_shared<qMaster::QueryManager>();
        }
        return *qm;
    }

    qMaster::AsyncQueryManager& getAsyncManager(int session) {
        return *(getSessionManager().getSession(session));
    }
}

void qMaster::initDispatcher() {
    xrdInit();
}

/// @param session Int for the session (the top-level query)
/// @param chunk Chunk number within this session (query)
/// @param str Query string (c-string)
/// @param len Query string length
/// @param savePath File path (with name) which will store the result (file, not dir)
/// @return a token identifying the session
int qMaster::submitQuery(int session, int chunk, char* str, int len, 
                         char* savePath, std::string const& resultName) {
    TransactionSpec t;
    AsyncQueryManager& qm = getAsyncManager(session);
    std::string const hp = qm.getXrootdHostPort();
    t.chunkId = chunk;
    t.query = std::string(str, len);
    t.bufferSize = 8192000;
    t.path = qMaster::makeUrl(hp.c_str(), "query", chunk);
    t.savePath = savePath;
    return submitQuery(session, TransactionSpec(t), resultName);
}

/// @param session Int for the session (the top-level query)
/// @param chunk Chunk number within this session (query)
/// @param str Query string (c-string)
/// @param len Query string length
/// @param savePath File path (with name) which will store the result (file, not dir)
/// @return a token identifying the session
int qMaster::submitQueryMsg(int session, char* dbName, int chunk,
                            char* str, int len, 
                            char* savePath, std::string const& resultName) {
    // Most dbName, chunk, resultName can be derived from msg, 
    // but we avoid unpacking it here.  Not sure how safe it is to
    // pass a python Protobuf msg through SWIG for use in c++.
    TransactionSpec t;
    AsyncQueryManager& qm = getAsyncManager(session);
    std::string const hp = qm.getXrootdHostPort();
    QservPath qp;
    qp.setAsCquery(dbName, chunk);
    std::string path=qp.path();
    t.chunkId = chunk;
    t.query = std::string(str, len);
    t.bufferSize = 8192000;
    t.path = qMaster::makeUrl(hp.c_str(), qp.path());
    t.savePath = savePath;
    return submitQuery(session, TransactionSpec(t), resultName);    
}

int qMaster::submitQuery(int session, qMaster::TransactionSpec const& s, 
                         std::string const& resultName) {
    int queryId = 0;
#if 1
    AsyncQueryManager& qm = getAsyncManager(session);
    qm.add(s, resultName); 
    //std::cout << "Dispatcher added  " << s.chunkId << std::endl;
#else
    QueryManager& qm = getManager(session);
    qm.add(s); 
#endif
    /* queryId = */ 
    return queryId;
}

qMaster::QueryState qMaster::joinQuery(int session, int id) {
    // Block until specific query id completes.
#if 1
    //AsyncQueryManager& qm = getAsyncManager(session);
#else
    //QueryManager& qm = getManager(session);
#endif
    //qm.join(id);
    //qm.status(id); // get status
    // If error, report
    return UNKNOWN; // FIXME: convert status to querystate.
}

qMaster::QueryState qMaster::tryJoinQuery(int session, int id) {
#if 0 // Not implemented yet
    // Just get the status and return it.
#if 1
    AsyncQueryManager& qm = getAsyncManager(session);
#else
    QueryManager& qm = getManager(session);
#endif
    if(qm.tryJoin(id)) {
        return SUCCESS; 
    } else {
        return ERROR;
    }   
#endif
    // FIXME...consider dropping this.
    // need return val.
    return UNKNOWN;
}

struct mergeStatus {
    mergeStatus(bool& success, bool shouldPrint_=false, int firstN_=5) 
        : isSuccessful(success), shouldPrint(shouldPrint_), 
          firstN(firstN_) {
        isSuccessful = true;
    }
    void operator() (qMaster::AsyncQueryManager::Result const& x) { 
        if(!x.second.isSuccessful()) {
            if(shouldPrint || (firstN > 0)) {
                std::cout << "Chunk " << x.first << " error " << std::endl
                          << "open: " << x.second.open 
                          << " qWrite: " << x.second.queryWrite 
                          << " read: " << x.second.read 
                          << " lWrite: " << x.second.localWrite << std::endl;
                --firstN;
            }
            isSuccessful = false;
        } else {
            if(shouldPrint) {
                std::cout << "Chunk " << x.first << " OK ("
                          << x.second.localWrite << ")\t";
            }
        }
    }
    bool& isSuccessful;
    bool shouldPrint;
    int firstN;
};

void qMaster::pauseReadTrans(int session) {
    AsyncQueryManager& qm = getAsyncManager(session);
    qm.pauseReadTrans();
}

void qMaster::resumeReadTrans(int session) {
    AsyncQueryManager& qm = getAsyncManager(session);
    qm.resumeReadTrans();
}

void qMaster::setupQuery(int session, std::string const& query) {
    
    // FIXME
    AsyncQueryManager& qm = getAsyncManager(session);
    QuerySession& qs = qm.getQuerySession();
    qs.setQuery(query);
}

std::string const& qMaster::getSessionError(int session) {
    static const std::string empty;
    // FIXME
    return empty;
}


qMaster::QueryState qMaster::joinSession(int session) {
    AsyncQueryManager& qm = getAsyncManager(session);
    qm.joinEverything();
    AsyncQueryManager::ResultDeque const& d = qm.getFinalState();
    bool successful;
    std::for_each(d.begin(), d.end(), mergeStatus(successful));
    
    if(successful) {
        std::cout << "Joined everything (success)" << std::endl;
        return SUCCESS;
    } else {
        std::cout << "Joined everything (failure!)" << std::endl;
        return ERROR;
    }
}

std::string const& 
qMaster::getQueryStateString(QueryState const& qs) {
    static const std::string unknown("unknown");
    static const std::string waiting("waiting");
    static const std::string dispatched("dispatched");
    static const std::string success("success");
    static const std::string error("error");
    switch(qs) {
    case UNKNOWN:
        return unknown;
    case WAITING:
        return waiting;
    case DISPATCHED:
        return dispatched;
    case SUCCESS:
        return success;
    case ERROR:
        return error;
    default:
        return unknown;
    }
}

std::string
qMaster::getErrorDesc(int session) {

    class _ErrMsgStr_ {
    public:
        _ErrMsgStr_(const std::string& name): _name(name) {}
            
        void add(int x) { 
            if (_ss.str().length() == 0) {
                _ss << _name << " failed for chunk(s):";
            }
            _ss << ' ' << x;
        }
        std::string toString() {
            return _ss.str();
        }
    private:
        const std::string _name;
        std::stringstream _ss;
    };    
    
    std::stringstream ss;
    AsyncQueryManager& qm = getAsyncManager(session);
    AsyncQueryManager::ResultDeque const& d = qm.getFinalState();
    AsyncQueryManager::ResultDequeCItr itr;
    _ErrMsgStr_ openV("open");
    _ErrMsgStr_ qwrtV("queryWrite");
    _ErrMsgStr_ readV("read");
    _ErrMsgStr_ lwrtV("localWrite");

    for (itr=d.begin() ; itr!=d.end(); ++itr) {
        if (itr->second.open <= 0) {
            openV.add(itr->first);
        } else if (itr->second.queryWrite <= 0) {
            qwrtV.add(itr->first);
        } else if (itr->second.read < 0) {
            readV.add(itr->first);
        } else if (itr->second.localWrite <= 0) {
            lwrtV.add(itr->first);
        }
    }
    // Handle open, write, read errors first. If we have 
    // any of these errors, we will get localWrite errors 
    // for every chunk, because we are not writing result, 
    // so don't bother reporting them.
    ss << openV.toString();
    ss << qwrtV.toString();
    ss << readV.toString();
    if (ss.str().empty()) {
        ss << lwrtV.toString();
    }
    return ss.str();
}

int qMaster::newSession(std::map<std::string,std::string> const& config) {
    AsyncQueryManager::Ptr m = 
        boost::make_shared<qMaster::AsyncQueryManager>(config);
    int id = getSessionManager().newSession(m);
    return id;
}

void qMaster::configureSessionMerger(int session, TableMergerConfig const& c) {
    getAsyncManager(session).configureMerger(c);    
}

std::string qMaster::getSessionResultName(int session) {
    return getAsyncManager(session).getMergeResultName();
}

void qMaster::discardSession(int session) {
    getSessionManager().discardSession(session);
    return; 
}

qMaster::XrdTransResult qMaster::getQueryResult(int session, int chunk) {
    return XrdTransResult();    // FIXME
}
