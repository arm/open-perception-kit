/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "writer.h"

#include <gst/gst.h>
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

static std::string serializeJob(const OpkCommJob &job) {
    nlohmann::json j;
    j["frame_counter"] = job.frame_counter;
    if (job.frameResults) {
        const auto packet = open_perception_kit::serialize(*job.frameResults);
        j["frame_results_encoding"] = "perception-frame-results+base64";
        j["frame_results_packet_b64"] = opk::base64Encode(packet);
    } else {
        j["frame_results_encoding"] = nullptr;
        j["frame_results_packet_b64"] = nullptr;
    }
    if (job.frame) {
        const auto &frame = *job.frame;
        j["frame"] = {{"encoding", "raw+base64"},
                      {"pixel_format", "BGRA"},
                      {"width", frame.width},
                      {"height", frame.height},
                      {"stride_bytes", size_t(frame.width) * 4},
                      {"data_b64", opk::base64Encode(frame.pixels)}};
    }
    auto line = j.dump();
    line.push_back('\n');
    return line;
}

void Writer::run() {
    OpkCommJob job;
    while (q.pop(job)) {
        try {
            publish(serializeJob(job));
        } catch (const std::exception &error) {
            GST_ERROR_OBJECT(m_self, "Failed to publish opkcomm record: %s", error.what());
        }
        job = {};
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
