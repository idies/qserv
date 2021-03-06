// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013-2016 AURA/LSST.
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
 /**
  * @file
  *
  * @brief A scheduler implementation that limits disk scans to one at
  * a time, but allows multiple queries to share I/O.
  *
  * @author Daniel L. Wang, SLAC
  */

// Class header

#include "wsched/ScanScheduler.h"

// System headers
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "global/Bug.h"
#include "wcontrol/Foreman.h"
#include "wsched/BlendScheduler.h"
#include "wsched/ChunkDisk.h"
#include "wsched/ChunkTasksQueue.h"

namespace {
LOG_LOGGER _log = LOG_GET("lsst.qserv.wsched.ScanScheduler");
}

namespace lsst {
namespace qserv {
namespace wsched {

ScanScheduler::ScanScheduler(std::string const& name, int maxThreads, int maxReserve, int priority,
                             int maxActiveChunks, memman::MemMan::Ptr const& memMan,
                             int minRating, int maxRating, double maxTimeMinutes)
    : SchedulerBase{name, maxThreads, maxReserve, maxActiveChunks, priority},
      _memMan{memMan}, _minRating{minRating}, _maxRating{maxRating},
      _maxTimeMinutes{maxTimeMinutes} {
    //_taskQueue = std::make_shared<ChunkDisk>(_memMan); // keeping for testing.
    _taskQueue = std::make_shared<ChunkTasksQueue>(this, _memMan);
    assert(_minRating <= _maxRating);
}


void ScanScheduler::commandStart(util::Command::Ptr const& cmd) {
    wbase::Task::Ptr task = std::dynamic_pointer_cast<wbase::Task>(cmd);
    _infoChanged = true;
    if (task == nullptr) {
        LOGS(_log, LOG_LVL_WARN, "ScanScheduler::commandStart cmd failed conversion " << getName());
        return;
    }
    LOGS(_log, LOG_LVL_DEBUG, task->getIdStr() << " commandStart " << getName());
    // task was registered Inflight when getCmd() was called.
}

void ScanScheduler::commandFinish(util::Command::Ptr const& cmd) {
    wbase::Task::Ptr t = std::dynamic_pointer_cast<wbase::Task>(cmd);
    _infoChanged = true;
    if (t == nullptr) {
        LOGS(_log, LOG_LVL_WARN, "ScanScheduler::commandFinish cmd failed conversion " << getName());
        return;
    }
    std::lock_guard<std::mutex> guard(util::CommandQueue::_mx);
    --_inFlight;
    LOGS(_log, LOG_LVL_DEBUG, t->getIdStr() << " commandFinish " << getName()
                                  << " inFlight=" << _inFlight);
    _taskQueue->taskComplete(t);

    if (_memManHandleToUnlock != memman::MemMan::HandleType::INVALID) {
        LOGS(_log, LOG_LVL_DEBUG, "ScanScheduler::commandFinish unlocking handle=" << _memManHandleToUnlock);
        _memMan->unlock(_memManHandleToUnlock);
        _memManHandleToUnlock = memman::MemMan::HandleType::INVALID;
    }

    // Wait to unlock the tables until after the next call to _ready or commandFinish.
    // This is done in case only one thread is running on this scheduler as
    // we don't want to release the tables in case the next Task wants some of them.
    if (!_taskQueue->empty()) {
        _memManHandleToUnlock = t->getMemHandle();
        LOGS(_log, LOG_LVL_DEBUG, t->getIdStr() << " setting handleToUnlock handle=" << _memManHandleToUnlock);
    } else {
        LOGS(_log, LOG_LVL_DEBUG, t->getIdStr() << " ScanScheduler::commandFinish unlocking handle="
              << t->getMemHandle());
        _memMan->unlock(t->getMemHandle()); // Nothing on the queue, no reason to wait.
    }

    _decrChunkTaskCount(t->getChunkId());
    if (_taskQueue->nextTaskDifferentChunkId()) {
        applyPriority();
    }
    // Whenever a Task finishes, all sleeping threads need to check if resources
    // are available to run new Tasks.
    _cv.notify_all();
}


/// Returns true if there is a Task ready to go and we aren't up against any limits.
bool ScanScheduler::ready() {
    std::lock_guard<std::mutex> lock(util::CommandQueue::_mx);
    return _ready();
}

/// Precondition: _mx is locked
/// Returns true if there is a Task ready to go and we aren't up against any limits.
bool ScanScheduler::_ready() {
    bool logStuff = false;
    if (_infoChanged) {
        _infoChanged = false;
        logStuff = true;
        LOGS(_log, LOG_LVL_DEBUG, "ScanScheduler::_ready name="<< getName() << " inFlight="
             << _inFlight << " maxThreads=" << _maxThreads << " adj=" << _maxThreadsAdj
             << " activeChunks=" << getActiveChunkCount());
    }
    if (_inFlight >= maxInFlight()) {
        if (logStuff) {
            LOGS(_log, LOG_LVL_DEBUG, "ScanScheduler::_ready too many in flight "
                 << _inFlight);
        }
        return false;
    }

    // Only run this test if _taskQueue is a ChunkDisk. ChunkTasksQueue does this internally.
    if (std::dynamic_pointer_cast<ChunkDisk>(_taskQueue) != nullptr
          &&_taskQueue->nextTaskDifferentChunkId()) {
        auto activeChunkCount = getActiveChunkCount();
        auto maxActiveChunks = getMaxActiveChunks();
        if (activeChunkCount >= maxActiveChunks) {
            if (logStuff) {
                LOGS(_log, LOG_LVL_DEBUG, "ScanScheduler::_ready too many ActiveChunks "
                     << activeChunkCount << ">=" << maxActiveChunks);
            }
            return false;
        }
    }

    bool useFlexibleLock = (_inFlight < 1);
    auto rdy = _taskQueue->ready(useFlexibleLock); // Only returns true if MemMan grants resources.
    bool logMemStats = false;
    // If ready failed, holding onto this is unlikely to help, otherwise the new Task now has its own handle.
    if (_memManHandleToUnlock != memman::MemMan::HandleType::INVALID) {
        LOGS(_log, LOG_LVL_DEBUG, "ScanScheduler::_ready unlocking handle=" << _memManHandleToUnlock);
        _memMan->unlock(_memManHandleToUnlock);
        _memManHandleToUnlock = memman::MemMan::HandleType::INVALID;
        logMemStats = true;
    }
    if (rdy || logMemStats) {
        logMemManStats();
    }
    return rdy;
}


std::size_t ScanScheduler::getSize() const {
    std::lock_guard<std::mutex> lock(util::CommandQueue::_mx);
    return _taskQueue->getSize();
}


util::Command::Ptr ScanScheduler::getCmd(bool wait)  {
    std::unique_lock<std::mutex> lock(util::CommandQueue::_mx);
    if (wait) {
        util::CommandQueue::_cv.wait(lock, [this](){return _ready();});
    } else if (!_ready()) {
        return nullptr;
    }
    bool useFlexibleLock = (_inFlight < 1);
    auto task = _taskQueue->getTask(useFlexibleLock);
    if (task != nullptr) {
        ++_inFlight; // in flight as soon as it is off the queue.
        LOGS(_log, LOG_LVL_DEBUG, task->getIdStr() << " getCmd " << getName() << " inflight=" << _inFlight);
        _infoChanged = true;
        _decrCountForUserQuery(task->getQueryId());
        _incrChunkTaskCount(task->getChunkId());
    }
    return task;
}


void ScanScheduler::queCmd(util::Command::Ptr const& cmd) {
    wbase::Task::Ptr t = std::dynamic_pointer_cast<wbase::Task>(cmd);
    if (t == nullptr) {
        LOGS(_log, LOG_LVL_WARN, getName() << " queCmd could not be converted to Task or was nullptr");
        return;
    }
    std::lock_guard<std::mutex> lock(util::CommandQueue::_mx);
    auto uqCount = _incrCountForUserQuery(t->getQueryId());
    LOGS(_log, LOG_LVL_DEBUG, getName() << " queCmd " << t->getIdStr()
         << " uqCount=" << uqCount);
    t->setMemMan(_memMan);
    _taskQueue->queueTask(t);
    _infoChanged = true;
    util::CommandQueue::_cv.notify_all();
}


/// @returns - If task was removed from the queue, a pointer to the removed Task
///            is returned
wbase::Task::Ptr ScanScheduler::removeTask(wbase::Task::Ptr const& task) {
    // Check if task is in the queue.
    // _taskQueue has its own mutex to protect this.
    auto rmTask = _taskQueue->removeTask(task);
    bool inQueue = rmTask != nullptr;
    LOGS(_log, LOG_LVL_DEBUG, "removeTask " << task->getIdStr() << " inQueue=" << inQueue);
    if (inQueue) return rmTask;

    LOGS(_log, LOG_LVL_DEBUG, "removeTask " << task->getIdStr() << " not in queue");
    // Wasn't in the queue, could be in flight.
    /// The task can only leave the pool if it has been started, and there is a tiny
    /// window where the task could have been pulled from the queue but commandStart()
    /// has not been called and 'task' does not know its poolThread. 'task' will possibly
    /// gum up its scheduler by being slow, but nothing terrible should happen. Waiting
    /// and calling this function again is probably the best option if needed.
    auto poolThread = task->getAndNullPoolEventThread();
    if (poolThread != nullptr) {
        poolThread->leavePool(task);
    } else {
        LOGS(_log, LOG_LVL_DEBUG, "removeTask PoolEventThread was null, "
                "presumably already moved for large result.");
    }
    // If it was running, no Task pointer should be returned as it could
    // (erroneously) be scheduled to run again on a different scheduler.
    return nullptr;
}


void ScanScheduler::logMemManStats() {
    auto s = _memMan->getStatistics();
    LOGS(_log, LOG_LVL_DEBUG, "bMax=" << s.bytesLockMax
         << " bLocked=" << s.bytesLocked
         << " bReserved=" << s.bytesReserved
         << " FSets=" << s.numFSets
         << " files=" << s.numFiles
         << " ReqF=" << s.numReqdFiles
         << " FlxF=" << s.numFlexFiles
         << " FlxLck=" << s.numFlexLock
         << " lckCalls=" << s.numLocks
         << " errs=" << s.numErrors);
}

}}} // namespace lsst::qserv::wsched
