/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Targets.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pek::log {

class Logger {
  public:
    static constexpr std::size_t BufferCapacity = 1024;

    explicit Logger(Targets targets);
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    void write(Level level, std::string &&message);
    std::vector<TargetType> getEnabledTargets();
    bool setTargetState(TargetType type, bool enabled);

    /// Waits until every record accepted before this call has been dispatched or overwritten by
    /// the drop-oldest buffer policy, then flushes the enabled targets. A sequence boundary is used
    /// instead of waiting for an empty buffer so concurrent producers cannot extend the wait
    /// indefinitely. Records accepted after the initial boundary set by this function will not be
    /// flushed.
    void flush();

  private:
    // Adds the record to the log buffer
    void appendRecord(Record &&record);
    // reads out the oldest record from the log buffer
    Record takeOldestRecord();
    // Return true if the records before the boundary parameter are all written out to the log
    // targets.
    bool recordsBeforeBoundaryProcessed(std::uint64_t boundary) const;
    // The function executed by the worker
    void processRecords();
    void writeToEnabledTargets(const Record &record);
    void flushEnabledTargets();

    // Protects the circular buffer, sequence numbers, in-flight record state, and stop request.
    // Producers, flush callers, the worker, and the destructor each release their own lock.
    // A condition-variable wait temporarily unlocks this mutex and reacquires it before returning.
    std::mutex m_bufferMutex;

    // The worker waits here until write() adds a record or the destructor requests shutdown.
    // write() and the destructor notify after updating the protected state.
    std::condition_variable m_recordsAvailable;

    // flush() callers wait with the help of this condition variable until records before their
    // sequence boundary are no longer pending in the processRecords function. The worker notifies
    // all waiters after it finishes dispatching each in-flight record.
    std::condition_variable m_recordsProcessed;

    // Buffer for the logs
    std::array<Record, BufferCapacity> m_records;

    std::size_t m_oldestRecordIndex{0};
    std::size_t m_bufferedRecordCount{0};
    std::uint64_t m_nextSequence{0};
    bool m_recordInProgress{false};
    std::uint64_t m_inFlightSequence{0};
    bool m_stopRequested{false};

    // Protects target enabled states and serializes target write and flush operations. API callers
    // lock it to inspect or change target state and during flush; the worker locks it during
    // dispatch and shutdown flush. Each thread releases its own lock.
    std::mutex m_targetsMutex;
    Targets m_targets;
    std::thread m_worker;
};

} // namespace pek::log
