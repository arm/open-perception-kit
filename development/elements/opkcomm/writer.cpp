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

#include <mutex>
#include <unistd.h>
#include <utility>

#include <opk/Base64.h>
#include <opk/FrameResults.h>

#include <nlohmann/json.hpp>

bool JobQueue::try_push(OpkCommJob &&job) {

    std::scoped_lock lock(m_mutex);

    if (m_stopped || m_jobs.size() >= m_capacity)
        return false;

    m_jobs.emplace_back(std::move(job));

    m_jobsAvailable.notify_one();
    return true;
}

bool JobQueue::pop(OpkCommJob &job) {
    std::unique_lock lock(m_mutex);

    m_jobsAvailable.wait(lock, [this] { return m_stopped || !m_jobs.empty(); });

    if (m_jobs.empty())
        return false;

    job = std::move(m_jobs.front());

    m_jobs.pop_front();

    return true;
}

void JobQueue::stop() {
    std::scoped_lock lock(m_mutex);
    m_stopped = true;
    m_jobs.clear();
    m_jobsAvailable.notify_all();
}

void JobQueue::reset() {
    std::scoped_lock lock(m_mutex);
    m_jobs.clear();
    m_stopped = false;
}

bool Writer::check_running() {
    if (!m_running) {
        return start();
    } else {
        return true;
    }
}

bool Writer::start() {
    auto success = false;
    if (!m_running) {
        m_queue.reset();
        success = io_open();

        if (success) {
            m_running = true;
            m_worker = std::jthread([this] { run(); });
        }
    }

    return success;
}

void Writer::stop() {
    if (m_running) {
        m_queue.stop();
        if (m_worker.joinable())
            m_worker.join();
        m_running = false;

        io_close();
    }
}

void Writer::run() {
    using json = nlohmann::json;

    OpkCommJob job;
    while (m_queue.pop(job)) {

        json record;

        record["frame_counter"] = job.frame_counter;
        if (job.frameResults) {
            const auto packet = open_perception_kit::serialize(*job.frameResults);
            record["frame_results_encoding"] = "perception-frame-results+base64";
            record["frame_results_packet_b64"] = opk::base64Encode(packet);
        } else {
            record["frame_results_encoding"] = nullptr;
            record["frame_results_packet_b64"] = nullptr;
        }

        std::string line = record.dump();
        line.push_back('\n');

        publish(line);
    }
}

bool Writer::send(OpkCommJob &&job) {
    auto success = false;
    if (m_running) {
        success = m_queue.try_push(std::move(job));
    } else {
        success = false;
    }

    return success;
}
