/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "writer.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <unistd.h>
#include <utility>

#include <pek/FrameResults.h>

#include <nlohmann/json.hpp>

namespace {

std::string base64Encode(std::span<const uint8_t> data) {
    static constexpr std::array<char, 65> table =
        std::to_array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    std::string out;
    out.reserve(((data.size() + 2U) / 3U) * 4U);

    size_t i = 0;
    while (i + 3U <= data.size()) {
        const uint32_t v =
            (uint32_t(data[i]) << 16U) | (uint32_t(data[i + 1U]) << 8U) | uint32_t(data[i + 2U]);
        out.push_back(table[(v >> 18U) & 0x3FU]);
        out.push_back(table[(v >> 12U) & 0x3FU]);
        out.push_back(table[(v >> 6U) & 0x3FU]);
        out.push_back(table[v & 0x3FU]);
        i += 3U;
    }

    const size_t rem = data.size() - i;
    if (rem == 1U) {
        const uint32_t v = uint32_t(data[i]) << 16U;
        out.push_back(table[(v >> 18U) & 0x3FU]);
        out.push_back(table[(v >> 12U) & 0x3FU]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2U) {
        const uint32_t v = (uint32_t(data[i]) << 16U) | (uint32_t(data[i + 1U]) << 8U);
        out.push_back(table[(v >> 18U) & 0x3FU]);
        out.push_back(table[(v >> 12U) & 0x3FU]);
        out.push_back(table[(v >> 6U) & 0x3FU]);
        out.push_back('=');
    }

    return out;
}

} // namespace

bool JobQueue::try_push(PekCommJob &&j) {

    std::scoped_lock lk(m_);

    if (stop_ || q_.size() >= max_)
        return false;

    q_.emplace_back(std::move(j));

    cv_.notify_one();
    return true;
}

bool JobQueue::pop(PekCommJob &out) {
    std::unique_lock lk(m_);

    cv_.wait(lk, [this] { return stop_ || !q_.empty(); });

    if (q_.empty())
        return false;

    out = std::move(q_.front());

    q_.pop_front();

    return true;
}

void JobQueue::stop() {
    std::scoped_lock lk(m_);
    stop_ = true;
    q_.clear();
    cv_.notify_all();
}

void JobQueue::reset() {
    std::scoped_lock lk(m_);
    q_.clear();
    stop_ = false;
}

bool Writer::check_running() {
    if (!running) {
        return start();
    } else {
        return true;
    }
}

bool Writer::start() {
    auto ret = false;
    if (!running) {
        q.reset();
        ret = io_open();

        if (ret) {
            running = true;
            th = std::jthread([this] { run(); });
        }
    }

    return ret;
}

void Writer::stop() {
    if (running) {
        q.stop();
        if (th.joinable())
            th.join();
        running = false;

        io_close();
    }
}

void Writer::run() {
    using json = nlohmann::json;

    PekCommJob job;
    while (q.pop(job)) {

        json j;

        j["frame_counter"] = job.frame_counter;
        if (job.frameResults) {
            const auto packet = perception::serialize(*job.frameResults);
            j["frame_results_encoding"] = "perception-frame-results+base64";
            j["frame_results_packet_b64"] = base64Encode(packet);
        } else {
            j["frame_results_encoding"] = nullptr;
            j["frame_results_packet_b64"] = nullptr;
        }

        std::string line = j.dump();
        line.push_back('\n');

        publish(line);
    }
}

bool Writer::send(PekCommJob &&job) {
    auto ret = false;
    if (running) {
        ret = q.try_push(std::move(job));
    } else {
        ret = false;
    }

    return ret;
}
