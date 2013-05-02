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

// SqlParseRunner is the top-level manager for everything attached to
// parsing the top-level SQL query. Much work (handling parse events)
// is delegated to other classes that are connected to the parser in
// this class. 
// Parse handlers implemented here: 
// LimitHandler 
// OrderByHandler
// FromHandler 
// SpatialTableNotifier
// HintTupleProcessor


// Standard
#include <functional>
#include <cstdio>
#include <strings.h>

// Boost
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

// Local (placed in src/)
#include "SqlSQL2Parser.hpp" 
#include "SqlSQL2Lexer.hpp"

#include "lsst/qserv/master/Callback.h"
#include "lsst/qserv/master/SqlParseRunner.h"
#include "lsst/qserv/master/Substitution.h"
#include "lsst/qserv/master/parseTreeUtil.h"
#include "lsst/qserv/master/stringUtil.h"
#include "lsst/qserv/master/TableNamer.h"
#include "lsst/qserv/master/TableRemapper.h"
#include "lsst/qserv/master/SpatialUdfHandler.h"
#include "lsst/qserv/master/parseExceptions.h"
#include "lsst/qserv/master/MetadataCache.h"

// namespace modifiers
namespace qMaster = lsst::qserv::master;
using std::stringstream;

// Forward declarations
namespace lsst {
namespace qserv {
namespace master {
    boost::shared_ptr<lsst::qserv::master::MetadataCache> getMetadataCache(int);
}}}

// Anonymous helpers
namespace {
class SelectCallback : public lsst::qserv::master::Callback {
    public:
        typedef boost::shared_ptr<SelectCallback> Ptr;
        SelectCallback(qMaster::Templater& t) :_t(t) { }
        void operator()() {
            _t.signalFromStmtBegin();
        }
    private:
        qMaster::Templater& _t;
    };

    std::string writeAsUnion(std::string const& query, 
                             qMaster::StringMap const& xMap, 
                             std::string const& delimiter) {
        qMaster::Substitution s(query, delimiter, false);
        return query + " UNION " + s.transform(xMap);
    }

    std::string writeSub(std::string const& query, 
                         qMaster::StringMap const& xMap, 
                         std::string const& delimiter) {
        qMaster::Substitution s(query, delimiter, false);
        return  s.transform(xMap);
    }

} // anonymous namespace

// Helpers
////////////////////////////////////////////////////////////////////////
// LimitHandler : Handle "LIMIT n" parse events
////////////////////////////////////////////////////////////////////////
class lsst::qserv::master::LimitHandler : public VoidOneRefFunc {
public: 
    LimitHandler(qMaster::SqlParseRunner& spr) : _spr(spr) {}
    virtual ~LimitHandler() {}
    virtual void operator()(antlr::RefAST i) {
        std::stringstream ss(i->getText());
        int limit;
        ss >> limit;
        _spr._setLimitForHandler(limit);
        //std::cout << "Got limit -> " << limit << std::endl;            
    }
private:
    qMaster::SqlParseRunner& _spr;
};
////////////////////////////////////////////////////////////////////////
// OrderByHandler : Handle "ORDER BY colname" events
////////////////////////////////////////////////////////////////////////
class lsst::qserv::master::OrderByHandler : public VoidOneRefFunc {
public: 
    OrderByHandler(qMaster::SqlParseRunner& spr) : _spr(spr) {}
    virtual ~OrderByHandler() {}
    virtual void operator()(antlr::RefAST i) {
        using qMaster::walkBoundedTreeString;
        using qMaster::getLastSibling;
        std::string cols = walkBoundedTreeString( i, getLastSibling(i));
        _spr._setOrderByForHandler(cols);
        //std::cout << "Got orderby -> " << cols << std::endl; 
    }
private:
    qMaster::SqlParseRunner& _spr;

};
////////////////////////////////////////////////////////////////////////
// SpatialTableNotifier : receive notification that query has chosen a spatial
// table.  This can then trigger the preparation of the table metadata to 
// provide the context for the where-clause manipulator to rewrite 
// appropriately. 
////////////////////////////////////////////////////////////////////////
class lsst::qserv::master::SqlParseRunner::SpatialTableNotifier
    : public lsst::qserv::master::Templater::Notifier {
public:
    SpatialTableNotifier(SqlParseRunner& spr) : _spr(spr) {}
    virtual void operator()(std::string const& refName, 
                            std::string const& name) {
        _spr.addMungedSpatial(name, refName);
        // std::cout << "Picked " << name << " as spatial table("
        //           << refName << ")." << std::endl;
    }
private:
    SqlParseRunner& _spr;
};

////////////////////////////////////////////////////////////////////////
// FromHandler : handle parse-acceptance of FROM... clause.  Rewrites
// spatial tables with aliases so that WHERE clause manipulation can
// utilize aliases if available.
////////////////////////////////////////////////////////////////////////
class lsst::qserv::master::SqlParseRunner::FromHandler : public VoidVoidFunc {
public: 
    // A functor to perform the rewrite
    class addToRewrite {
    public:
        addToRewrite(StringPairList& stm, StringMap const& tableMungeMap) 
            : _stm(stm),  _tableMungeMap(tableMungeMap) {}
        void operator()(StringPairList::value_type const& v) {
            // Lookup referent table using table name.
            std::string s = getFromMap(_tableMungeMap, v.second, blank);
            if(s.empty()) return;
            // std::cout << "FROM rewriting " << v.first << " " 
            //           << v.second << s << std::endl;
            _stm.push_back(StringPairList::value_type(v.first, s));
        }
        std::string blank;
        StringPairList& _stm;
        StringMap const& _tableMungeMap;
    };
    // A functor to filter out nop mappings (key==value)
    class isNonTrivialMapping {
    public: 
        bool operator()(StringMap::value_type const& v) const { 
            return v.first != v.second; 
        }
        static isNonTrivialMapping const& getStatic() { 
            static isNonTrivialMapping s; return s; }
    };

    // FromHandler
    FromHandler(qMaster::SqlParseRunner& spr) : _spr(spr) {}
    virtual ~FromHandler() {}
    virtual void operator()() {
        StringPairList const& tableAliases = _spr._aliasMgr.getTableAliases();
        Templater::addAliasFunc f(_spr._templater);
        // Pass aliases over to templater.
        forEachFirst(_spr._aliasMgr.getTableAliasMap(), f, 
                     isNonTrivialMapping::getStatic());


        // Handle names, now that aliases are known.
        // Instead of mungemap, use tablenamer.
        _spr._tableNamer->acceptAliases(tableAliases);
        // SpatialUdfHandler reads from tableNamer

        _spr._templater.processNames(); 
        _spr._tableListHandler->processJoin();
        _spr._templater.signalFromStmtEnd();
    }
private:
    qMaster::SqlParseRunner& _spr;
};

/// HintTupleProcessor: Function object that ingests config
/// entries from query.hints
/// e.g. query.hints=box,0,0,5,1;circle,1,1,1;
/// Split by ',' and then add the directives to the spatial handler.
/// 
class lsst::qserv::master::SqlParseRunner::HintTupleProcessor {
public:
    HintTupleProcessor(SqlParseRunner& spr) : _spr(spr) {}
    void operator()(std::string const& s) {
        int vecSize;

        _vec.clear();
        tokenizeInto(s, ",", _vec, passFunc<std::string>());
        vecSize = _vec.size();
        if(vecSize < 2) {
            if(vecSize == 0) {
                return; // Nothing to do.
            }
            std::cout << "Error, badly formed partition col spec: " << s 
                      << std::endl;
            return;
        }
        // FIXME don't convert now. bigints can't fit in double.
        _spr.addHintExpr(_vec);
    }
    
private:
    std::vector<std::string> _vec;
    strToDoubleFunc toDouble;
    SqlParseRunner& _spr;
};

////////////////////////////////////////////////////////////////////////
// class SqlParseRunner
////////////////////////////////////////////////////////////////////////
boost::shared_ptr<qMaster::SqlParseRunner> 
qMaster::SqlParseRunner::newInstance(std::string const& statement, 
                                     std::string const& delimiter,
                                     qMaster::StringMap const& config,
                                     int metaCacheId) {
    return boost::shared_ptr<SqlParseRunner>(new SqlParseRunner(statement, 
                                                                delimiter,
                                                                config,
                                                                metaCacheId));
}

qMaster::SqlParseRunner::SqlParseRunner(std::string const& statement, 
                                        std::string const& delimiter,
                                        qMaster::StringMap const& config,
                                        int metaCacheId) :
    _statement(statement),
    _stream(statement, stringstream::in | stringstream::out),
    _factory(new ASTFactory()),
    _lexer(new SqlSQL2Lexer(_stream)),
    _parser(new SqlSQL2Parser(*_lexer)),
    _delimiter(delimiter),
    _templater(delimiter, _factory.get()),
    _aliasMgr(),
    _aggMgr(_aliasMgr),
    _metaCacheId(metaCacheId),
    _tableNamer(new TableNamer(metaCacheId)),
    _spatialUdfHandler(new SpatialUdfHandler(_factory.get(),
                                             _tableConfigMap, 
                                             *_tableNamer))
{ 
    try {
        _readConfig(config);
    } catch (std::string msg) {
        _errorMsg = "Parser: " + msg;
    }
    //std::cout << "(int)PARSING:"<< statement << std::endl;
}

void qMaster::SqlParseRunner::setup(std::list<std::string> const& names) {
    _templater.setKeynames(names.begin(), names.end());
    // Setup parser
    _parser->_columnRefHandler = _templater.newColumnHandler();
    _parser->_qualifiedNameHandler = _templater.newTableHandler();
    _tableListHandler = _templater.newTableListHandler();
    _parser->_tableListHandler = _tableListHandler;
    _parser->_setFctSpecHandler = _aggMgr.getSetFuncHandler();
    _parser->_columnAliasHandler = _aliasMgr.getColumnAliasHandler();
    _parser->_tableAliasHandler = _aliasMgr.getTableAliasHandler();
    _parser->_selectListHandler = _aggMgr.getSelectListHandler();
    _parser->_selectStarHandler = _aggMgr.newSelectStarHandler();
    _parser->_groupByHandler = _aggMgr.getGroupByHandler();
    _parser->_groupColumnHandler = _aggMgr.getGroupColumnHandler();
    _parser->_limitHandler.reset(new LimitHandler(*this));
    _parser->_orderByHandler.reset(new OrderByHandler(*this));
    _parser->_fromHandler.reset(new FromHandler(*this));
    _parser->_fromWhereHandler = _spatialUdfHandler->getFromWhereHandler();
    _parser->_whereCondHandler= _spatialUdfHandler->getWhereCondHandler();
    _parser->_qservRestrictorHandler = _spatialUdfHandler->getRestrictorHandler();
    _parser->_qservFctSpecHandler= _spatialUdfHandler->getFctSpecHandler();

    // Listen for select* or select <col_list> parse
    SelectCallback::Ptr sCallback(new SelectCallback(_templater));
    _aggMgr.listenSelectReceived(sCallback);
    _aliasMgr.addTableAliasFunction(_tableNamer->getTableAliasFunc());
}

std::string qMaster::SqlParseRunner::getParseResult() {
    if(_errorMsg.empty() && _parseResult.empty()) {
        _computeParseResult();
    }
    return _parseResult;
}
std::string qMaster::SqlParseRunner::getAggParseResult() {
    if(_errorMsg.empty() && _aggParseResult.empty()) {
        _computeParseResult();
    }
    return _aggParseResult;
}

bool qMaster::SqlParseRunner::getHasChunks() const { 
    return _tableNamer->getHasChunks();
}

bool qMaster::SqlParseRunner::getHasSubChunks() const { 
    return _tableNamer->getHasSubChunks();
}

void qMaster::SqlParseRunner::_computeParseResult() {
    StringList badDbs;
    try {
        _parser->initializeASTFactory(*_factory);
        _parser->setASTFactory(_factory.get());
        _parser->sql_stmt();
        _aggMgr.postprocess(_aliasMgr.getInvAliases());
        badDbs = _tableNamer->getBadDbs();
        RefAST ast = _parser->getAST();
        if (ast) {
            //std::cout << "fixupSelect " << getFixupSelect();
            //std::cout << "passSelect " << getPassSelect();
            // ";" is not in the AST, so add it back.
            // Apply substitution.
            TableRemapper tr(*_tableNamer, _metaCacheId, _delimiter);
            //std::cout << "PRE:: " << walkTreeString(ast) << std::endl;
            walkTreeSubstitute(ast, tr.getMap());
            //std::cout << "POST:: " << walkTreeString(ast) << std::endl;;
            _parseResult = walkTreeString(ast);
            _aggMgr.applyAggPass();
            _aggParseResult = walkTreeString(ast);
            if(_tableNamer->getHasSubChunks()) {
                StringMap overlapMap = tr.getPatchMap();
                _aggParseResult = writeAsUnion(_aggParseResult, 
                                               overlapMap, _delimiter);
                _parseResult = writeAsUnion(_parseResult,
                                            overlapMap, _delimiter);
            } 
            _aggParseResult += ";";
            _parseResult += ";";
            _mFixup.select = _aggMgr.getFixupSelect();
            _mFixup.post = _aggMgr.getFixupPost();
            //"", /* FIXME need orderby */
            _mFixup.needsFixup = _aggMgr.getHasAggregate() 
                || (_mFixup.limit != -1) || (!_mFixup.orderBy.empty());
        } else {
            _errorMsg = "Error: no AST from parse";
        }
    } catch( antlr::ANTLRException& e ) {
        _errorMsg =  "Parse exception: " + e.toString();
    } catch( UnsupportedSyntaxError& e ) {
        _errorMsg = e.what();
        
    } catch( std::exception& e ) {
        _errorMsg = std::string("Exception: ") + e.what();
    } catch(...) {
        _errorMsg = std::string("Unknown exception. System not stable.");
    }
    if(badDbs.size() > 0) {
        _errorMsg += _interpretBadDbs(badDbs);
    }
    return; 
}

// Interprets a list of bad dbs and computes an appropriate error message.
std::string qMaster::SqlParseRunner::_interpretBadDbs(qMaster::StringList const& bd) {
    std::stringstream ss;
    typedef qMaster::StringList::const_iterator ConstIter;
    ConstIter end = bd.end();
    bool hasDefBad = false;  // default db is bad.
    bool hasRealBad = false; // specified db is bad.
    coercePrint<std::string> cp(ss, ",");
    for(ConstIter ci = bd.begin(); ci != end; ++ci) {
        if(ci->empty()) {
            hasDefBad = true;
        } else {
            if(!hasRealBad) {
                ss << " Query references prohibited dbs: ";
                hasRealBad = true;
            }              
            cp(*ci);
        }
    }
    if(hasDefBad) { 
        return "No database selected. " + ss.str();
    } else {
        return ss.str();
    }        
}

void qMaster::SqlParseRunner::_makeOverlapMap() {
    qMaster::Templater::IntMap im = _tableListHandler->getUsageCount();
    qMaster::Templater::IntMap::iterator e = im.end();
    for(qMaster::Templater::IntMap::iterator i = im.begin(); i != e; ++i) {
        _overlapMap[i->first+"_sc2"] = i->first + "_sfo";
    }

}

bool qMaster::SqlParseRunner::getHasAggregate() {
    if(_errorMsg.empty() && _parseResult.empty()) {
        _computeParseResult();
    }
    return _aggMgr.getHasAggregate();
}

void qMaster::SqlParseRunner::addMungedSpatial(std::string const& mungedTable,
                                               std::string const& refTable) {
    std::string blank;
    std::string s = getFromMap(_mungeMap, mungedTable, blank);
    if(s != blank) {
        if(s != refTable) {
            std::cerr << "ERROR! Conflicting munged referent: " 
                      << mungedTable << " -> " << s << " (existing), "
                      << refTable << " (new)" << std::endl;
        }
    } else {
        _mungeMap[mungedTable] = refTable;
    }
}

void qMaster::SqlParseRunner::updateTableConfig(std::string const& tName, 
                                                qMaster::StringMap const& m) {
    _tableConfigMap[tName] = m;
}

void qMaster::SqlParseRunner::addHintExpr(std::vector<std::string> const& vec) {
    _spatialUdfHandler->addExpression(vec);
}


void qMaster::SqlParseRunner::_readConfig(qMaster::StringMap const& m) {
    std::string blank;
    std::list<std::string> tokens;
    std::string defaultDb;
    IntMap whiteList;

    defaultDb = getFromMap(m, "table.defaultdb", blank); // client DB context

    tokens.clear();
    tokenizeInto(getFromMap(m,"query.hints", blank), ";", tokens,
                 passFunc<std::string>());
    for_each(tokens.begin(), tokens.end(), HintTupleProcessor(*this));

    // FIXME: Much of this could be done at startup and cached.
    tokenizeInto(getFromMap(m, "table.alloweddbs", blank), ",", tokens, 
                 passFunc<std::string>());
    if(tokens.size() > 0) {
        fillMapFromKeys(tokens, whiteList);
    } else {
        std::cout << "WARNING!  No dbs in whitelist. Using LSST." << std::endl;
        whiteList["LSST"] = 1;
    }    

    _templater.setup(whiteList, defaultDb, _metaCacheId);
    _tableNamer->setDefaultDb(defaultDb);
    std::vector<std::string> allowedDbs = getMetadataCache(_metaCacheId)->getAllowedDbs();
    std::vector<std::string>::const_iterator allowedDb;
    for (allowedDb=allowedDbs.begin() ; allowedDb!=allowedDbs.end() ; ++allowedDb) {
        std::vector<std::string> tables = getMetadataCache(_metaCacheId)->getChunkedTables(*allowedDb);
        std::vector<std::string>::const_iterator table;
        for (table=tables.begin() ; table!=tables.end() ; ++table) {
            std::vector<std::string> v = getMetadataCache(_metaCacheId)->getPartitionCols(*allowedDb, *table);
            StringMap sm;
            sm["raCol"] = v[0];
            sm["declCol"] = v[1];
            sm["objectIdCol"] = v[2];
            //std::cout << "***** qms-based for " << *allowedDb << "." << *table << ": " << sm["raCol"] 
            //          << ", " << sm["declCol"] << ", "<< sm["objectIdCol"] << std::endl;
            updateTableConfig(*table, sm);
        }
    }
}
