// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013-2016 LSST Corporation.
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

#ifndef LSST_QSERV_WSCHED_QUERYSTATISTICS_H
#define LSST_QSERV_WSCHED_QUERYSTATISTICS_H

// System headers

// Qserv headers
#include "wbase/Task.h"

namespace lsst {
namespace qserv {
namespace wpublish {

class Queries;

class QueryStatistics {
public:
    using Ptr = std::shared_ptr<QueryStatistics>;
    explicit QueryStatistics(QueryId const& queryId) : _queryId{_queryId} {}

    void addTask(wbase::Task::Ptr const& task);

    friend class Queries;

private:
    std::mutex _mx;
    QueryId const _queryId;
    std::chrono::system_clock::time_point _touched{std::chrono::system_clock::now()};

    int _tasksCompleted{0};
    int _tasksRunning{0};
    int _tasksBooted{0}; ///< Number of Tasks booted for being too slow.

    double _totalCompletionTime{0.0};

    std::map<int, wbase::Task::Ptr> _taskMap;
};


class SchedulerChunkStatistics {
public:

private:
    int _tasksCompleted{0};
    double _totalCompletionTime{0.0};
    // effects of system load???  &&&
    // effects of Tasks running for other chunks??? &&&
    // keep rolling average of past 100 tasks??? &&&
};


class Queries {
public:
    using Ptr = std::shared_ptr<Queries>;

    QueryStatistics::Ptr getStats(QueryId const& qId) const;

    void addTask(wbase::Task::Ptr const& task);
    void queuedTask(wbase::Task::Ptr const& task);
    void startedTask(wbase::Task::Ptr const& task);
    void finishedTask(wbase::Task::Ptr const& task);

//private: &&& make private
    mutable std::mutex _qStatsMtx; ///< protects _queryStats;
    std::map<QueryId, QueryStatistics::Ptr> _queryStats;
};


}}} // namespace lsst::qserv::wpublish
#endif // LSST_QSERV_WSCHED_QUERYSTATISTICS_H