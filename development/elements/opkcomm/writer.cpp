/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "writer.h"

#include <mutex>
#include <unistd.h>
#include <utility>

#include <opk/Base64.h>
#include <opk/FrameResults.h>

#include <nlohmann/json.hpp>

bool JobQueue::try_push(OpkCommJob &&j) {

    std::scoped_lock lk(m_);

    if (stop_ || q_.size() >= max_)
        return false;

    q_.emplace_back(std::move(j));

    cv_.notify_one();
    return true;
}

bool JobQueue::pop(OpkCommJob &out) {
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

    OpkCommJob job;
    while (q.pop(job)) {

        json j;

        j["frame_counter"] = job.frame_counter;
        if (job.frameResults) {
            const auto packet = open_perception_kit::serialize(*job.frameResults);
            j["frame_results_encoding"] = "perception-frame-results+base64";
            j["frame_results_packet_b64"] = opk::base64Encode(packet);
        } else {
            j["frame_results_encoding"] = nullptr;
            j["frame_results_packet_b64"] = nullptr;
        }

        std::string line = j.dump();
        line.push_back('\n');

        publish(line);
    }
}

bool Writer::send(OpkCommJob &&job) {
    auto ret = false;
    if (running) {
        ret = q.try_push(std::move(job));
    } else {
        ret = false;
    }

    return ret;
}
