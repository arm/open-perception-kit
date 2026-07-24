/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Logger.h"
#include "ConsoleOutputs.h"

#include <gtest/gtest.h>

#include <array>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TargetState {
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> messages;
    bool blockFirstWrite{false};
    bool firstWriteStarted{false};
    bool releaseFirstWrite{false};
    bool failFirstWrite{false};
    bool failFlush{false};
    std::size_t flushCount{0};
};

class RecordingTarget final : public pek::log::LogTarget {
  public:
    RecordingTarget(pek::log::LogTargetType type, std::shared_ptr<TargetState> state)
        : LogTarget(type, true), m_state(std::move(state)) {}

    void write(const pek::log::LogRecord &record) override {
        std::unique_lock lock(m_state->mutex);
        if (m_state->messages.empty() && m_state->blockFirstWrite) {
            m_state->firstWriteStarted = true;
            m_state->condition.notify_all();
            m_state->condition.wait(lock, [this] { return m_state->releaseFirstWrite; });
        }
        if (m_state->failFirstWrite) {
            m_state->failFirstWrite = false;
            throw std::runtime_error("target failure");
        }
        m_state->messages.push_back(record.m_message);
    }

    void flush() override {
        std::lock_guard lock(m_state->mutex);
        if (m_state->failFlush) {
            throw std::runtime_error("flush failure");
        }
        ++m_state->flushCount;
    }

  private:
    std::shared_ptr<TargetState> m_state;
};

std::unique_ptr<pek::log::LogTarget> makeTarget(pek::log::LogTargetType type,
                                                const std::shared_ptr<TargetState> &state) {
    return std::make_unique<RecordingTarget>(type, state);
}

void waitForFirstWrite(const std::shared_ptr<TargetState> &state) {
    std::unique_lock lock(state->mutex);
    state->condition.wait(lock, [&state] { return state->firstWriteStarted; });
}

void releaseFirstWrite(const std::shared_ptr<TargetState> &state) {
    {
        std::lock_guard lock(state->mutex);
        state->releaseFirstWrite = true;
    }
    state->condition.notify_all();
}

} // namespace

TEST(Logger, FlushWaitsForAcceptedRecordsAndFlushesTargets) {
    auto state = std::make_shared<TargetState>();
    pek::log::LogTargets targets;
    targets.push_back(makeTarget(pek::log::LogTargetType::Stdout, state));
    pek::log::Logger logger(std::move(targets));

    logger.write(pek::log::LogLevel::Info, "first");
    logger.write(pek::log::LogLevel::Info, "second");
    logger.flush();

    std::lock_guard lock(state->mutex);
    EXPECT_EQ(state->messages, (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(state->flushCount, 1U);
}

TEST(Logger, FullBufferDropsOldestBufferedRecord) {
    auto state = std::make_shared<TargetState>();
    state->blockFirstWrite = true;
    pek::log::LogTargets targets;
    targets.push_back(makeTarget(pek::log::LogTargetType::Stdout, state));
    pek::log::Logger logger(std::move(targets));

    logger.write(pek::log::LogLevel::Info, "in flight");
    waitForFirstWrite(state);
    for (std::size_t index = 0; index <= pek::log::Logger::BufferCapacity; ++index) {
        logger.write(pek::log::LogLevel::Info, std::to_string(index));
    }
    releaseFirstWrite(state);
    logger.flush();

    std::lock_guard lock(state->mutex);
    ASSERT_EQ(state->messages.size(), pek::log::Logger::BufferCapacity + 1);
    EXPECT_EQ(state->messages.front(), "in flight");
    EXPECT_EQ(state->messages[1], "1");
    EXPECT_EQ(state->messages.back(), std::to_string(pek::log::Logger::BufferCapacity));
}

TEST(Logger, FailureDisablesOnlyTheFailingTarget) {
    auto failingState = std::make_shared<TargetState>();
    failingState->failFirstWrite = true;
    auto healthyState = std::make_shared<TargetState>();
    pek::log::LogTargets targets;
    targets.push_back(makeTarget(pek::log::LogTargetType::Stdout, failingState));
    targets.push_back(makeTarget(pek::log::LogTargetType::Stderr, healthyState));
    pek::log::Logger logger(std::move(targets));

    logger.write(pek::log::LogLevel::Info, "first");
    logger.write(pek::log::LogLevel::Info, "second");
    logger.flush();

    EXPECT_EQ(logger.getEnabledTargets(), std::vector{pek::log::LogTargetType::Stderr});
    std::lock_guard lock(healthyState->mutex);
    EXPECT_EQ(healthyState->messages, (std::vector<std::string>{"first", "second"}));
}

TEST(Logger, FlushFailureDisablesOnlyTheFailingTarget) {
    auto failingState = std::make_shared<TargetState>();
    failingState->failFlush = true;
    auto healthyState = std::make_shared<TargetState>();
    pek::log::LogTargets targets;
    targets.push_back(makeTarget(pek::log::LogTargetType::Stdout, failingState));
    targets.push_back(makeTarget(pek::log::LogTargetType::Stderr, healthyState));
    pek::log::Logger logger(std::move(targets));

    logger.flush();

    EXPECT_EQ(logger.getEnabledTargets(), std::vector{pek::log::LogTargetType::Stderr});
    std::lock_guard lock(healthyState->mutex);
    EXPECT_EQ(healthyState->flushCount, 1U);
}

TEST(ConsoleOutput, ReportsStreamFlushFailure) {
    std::FILE *stream = std::fopen("/dev/full", "w");
    if (stream == nullptr) {
        GTEST_SKIP() << "/dev/full is unavailable";
    }

    std::array<char, BUFSIZ> streamBuffer{};
    EXPECT_EQ(std::setvbuf(stream, streamBuffer.data(), _IOFBF, streamBuffer.size()), 0);
    EXPECT_GE(std::fputs("buffered record", stream), 0);
    pek::log::ConsoleOutput output(pek::log::LogTargetType::Stdout, true, stream);

    EXPECT_THROW(output.flush(), std::system_error);
    EXPECT_EQ(std::fclose(stream), 0);
}

TEST(Logger, DestructorDrainsAndFlushesBufferedRecords) {
    auto state = std::make_shared<TargetState>();
    {
        pek::log::LogTargets targets;
        targets.push_back(makeTarget(pek::log::LogTargetType::Stdout, state));
        pek::log::Logger logger(std::move(targets));
        logger.write(pek::log::LogLevel::Info, "remaining");
    }

    std::lock_guard lock(state->mutex);
    EXPECT_EQ(state->messages, std::vector<std::string>{"remaining"});
    EXPECT_EQ(state->flushCount, 1U);
}
