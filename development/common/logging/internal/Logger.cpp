/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "Logger.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace opk::log {

Logger::Logger(Targets targets)
    : m_targets(std::move(targets)), m_worker(&Logger::processRecords, this) {}

Logger::~Logger() {
    {
        std::lock_guard lock(m_bufferMutex);
        m_stopRequested = true;
    }
    m_recordsAvailable.notify_one();
    m_worker.join();
}

void Logger::write(Level level, std::string &&message) {
    {
        std::lock_guard lock(m_bufferMutex);
        appendRecord(Record{level, std::move(message), m_nextSequence++});
    }
    m_recordsAvailable.notify_one();
}

std::vector<TargetType> Logger::getEnabledTargets() {
    std::lock_guard lock(m_targetsMutex);
    std::vector<TargetType> enabledTargets;
    enabledTargets.reserve(m_targets.size());
    for (const auto &target : m_targets) {
        if (target->isEnabled()) {
            enabledTargets.push_back(target->getType());
        }
    }
    return enabledTargets;
}

bool Logger::setTargetState(TargetType type, bool enabled) {
    std::lock_guard lock(m_targetsMutex);
    const auto matchingTarget =
        std::find_if(m_targets.begin(), m_targets.end(), [type](const auto &target) {
            return target->getType() == type;
        });
    if (matchingTarget == m_targets.end()) {
        return false;
    }
    (*matchingTarget)->setEnabled(enabled);
    return true;
}

void Logger::flush() {
    std::unique_lock bufferLock(m_bufferMutex);
    const std::uint64_t boundary = m_nextSequence;
    m_recordsProcessed.wait(bufferLock,
                            [this, boundary] { return recordsBeforeBoundaryProcessed(boundary); });
    bufferLock.unlock();

    std::lock_guard targetsLock(m_targetsMutex);
    flushEnabledTargets();
}

void Logger::appendRecord(Record &&record) {
    if (m_bufferedRecordCount == BufferCapacity) {
        m_records[m_oldestRecordIndex] = std::move(record);
        m_oldestRecordIndex = (m_oldestRecordIndex + 1) % BufferCapacity;
        return;
    }

    const std::size_t insertionIndex =
        (m_oldestRecordIndex + m_bufferedRecordCount) % BufferCapacity;
    m_records[insertionIndex] = std::move(record);
    ++m_bufferedRecordCount;
}

Record Logger::takeOldestRecord() {
    Record oldestRecord = std::move(m_records[m_oldestRecordIndex]);
    m_oldestRecordIndex = (m_oldestRecordIndex + 1) % BufferCapacity;
    --m_bufferedRecordCount;
    return oldestRecord;
}

bool Logger::recordsBeforeBoundaryProcessed(std::uint64_t boundary) const {
    const bool earlierRecordInFlight = m_recordInProgress && m_inFlightSequence < boundary;
    const bool earlierRecordBuffered =
        m_bufferedRecordCount > 0 && m_records[m_oldestRecordIndex].m_sequence < boundary;
    return !earlierRecordInFlight && !earlierRecordBuffered;
}

void Logger::processRecords() {
    while (true) {
        std::unique_lock bufferLock(m_bufferMutex);
        m_recordsAvailable.wait(bufferLock,
                                [this] { return m_stopRequested || m_bufferedRecordCount > 0; });
        // Graceful shutdown. When the desctructor is called, first every available log in the
        // buffer is written out and only then the worker is finished.
        if (m_stopRequested && m_bufferedRecordCount == 0) {
            break;
        }

        Record record = takeOldestRecord();
        m_recordInProgress = true;
        m_inFlightSequence = record.m_sequence;
        bufferLock.unlock();

        writeToEnabledTargets(record);

        bufferLock.lock();
        m_recordInProgress = false;
        bufferLock.unlock();
        m_recordsProcessed.notify_all();
    }

    std::lock_guard targetsLock(m_targetsMutex);
    flushEnabledTargets();
}

void Logger::writeToEnabledTargets(const Record &record) {
    std::lock_guard lock(m_targetsMutex);
    for (auto &target : m_targets) {
        if (!target->isEnabled()) {
            continue;
        }
        try {
            target->write(record);
        } catch (const std::exception &) {
            target->setEnabled(false);
        } catch (...) {
            target->setEnabled(false);
        }
    }
}

void Logger::flushEnabledTargets() {
    for (auto &target : m_targets) {
        if (!target->isEnabled()) {
            continue;
        }
        try {
            target->flush();
        } catch (const std::exception &) {
            target->setEnabled(false);
        } catch (...) {
            target->setEnabled(false);
        }
    }
}

} // namespace opk::log
