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
#include <pthread.h>
#include <signal.h>
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

bool JobQueue::pop(OpkCommJob &job, const std::function<bool()> &ready) {
    constexpr auto readinessPollInterval = std::chrono::milliseconds(10);
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_idle = true;
        m_jobsAvailable.notify_all();
        m_jobsAvailable.wait(lock, [this] { return m_stopped || (!m_paused && !m_jobs.empty()); });
        if (m_stopped)
            return false;
        m_idle = false;
        lock.unlock();
        const bool destinationReady = ready();
        lock.lock();
        if (m_stopped)
            return false;
        if (m_paused)
            continue;
        if (!destinationReady) {
            m_jobsAvailable.wait_for(lock, readinessPollInterval);
            continue;
        }
        job = std::move(m_jobs.front());
        m_jobs.pop_front();
        return true;
    }
}

bool JobQueue::pauseUntil(const std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(m_mutex);
    m_paused = true;
    m_jobsAvailable.notify_all();
    if (m_jobsAvailable.wait_until(lock, deadline, [this] { return m_idle || m_stopped; }) &&
        !m_stopped)
        return true;
    m_paused = false;
    m_jobsAvailable.notify_all();
    return false;
}

void JobQueue::resume() {
    const std::scoped_lock lock(m_mutex);
    m_paused = false;
    m_jobsAvailable.notify_all();
}

void JobQueue::transferTo(JobQueue &destination) {
    const std::scoped_lock lock(m_mutex, destination.m_mutex);
    destination.m_jobs = std::move(m_jobs);
}

void JobQueue::stop() {
    std::scoped_lock lock(m_mutex);
    m_stopped = true;
    m_jobs.clear();
    m_jobsAvailable.notify_all();
}

void JobQueue::reset(const bool paused) {
    std::scoped_lock lock(m_mutex);
    m_jobs.clear();
    m_stopped = false;
    m_paused = paused;
    m_idle = true;
}

bool Writer::start(const bool paused) {
    const std::scoped_lock lifecycleLock(m_lifecycleMutex);
    m_queue.reset(paused);
    if (!io_open())
        return false;
    m_failed = false;
    m_running = true;
    m_worker = std::jthread([this] { run(); });
    return true;
}

void Writer::stop() {
    m_running = false;
    m_queue.stop();
    const std::scoped_lock lifecycleLock(m_lifecycleMutex);
    if (m_worker.joinable())
        m_worker.join();
    io_close();
}

bool Writer::pauseUntil(const std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lifecycleLock(m_lifecycleMutex, std::defer_lock);
    if (!lifecycleLock.try_lock_until(deadline))
        return false;
    if (healthy() && m_queue.pauseUntil(deadline) && flush(deadline) && healthy())
        return true;
    m_queue.resume();
    return false;
}

void Writer::resume() {
    m_queue.resume();
}
void Writer::transferTo(Writer &destination) {
    m_queue.transferTo(destination.m_queue);
}

void Writer::run() {
    // A closed pipe/socket must report an element error, not terminate the application.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    using json = nlohmann::json;

    OpkCommJob job;
    while (m_queue.pop(job, [this] { return ready(); })) {

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

        if (!publish(line)) {
            fail();
            break;
        }
    }
}

void Writer::fail() {
    if (!m_running || m_failed.exchange(true))
        return;
    m_queue.stop();
    gst_element_call_async(
        GST_ELEMENT(m_self),
        [](GstElement *const element, gpointer) {
            GST_ELEMENT_ERROR(element,
                              RESOURCE,
                              WRITE,
                              ("Could not finish the current metadata record"),
                              (nullptr));
        },
        nullptr,
        nullptr);
}

bool Writer::send(OpkCommJob &&job) {
    return healthy() && m_queue.try_push(std::move(job));
}
