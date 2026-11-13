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

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <opk/FrameResults.h>

struct _GstOpkComm;

struct OpkCommJob {
    uint64_t frame_counter = 0;
    std::shared_ptr<const open_perception_kit::FrameResults> frameResults;
};

class JobQueue {
  public:
    explicit JobQueue(size_t capacity) : m_capacity(capacity) {}

    bool try_push(OpkCommJob &&job);
    bool pop(OpkCommJob &job);

    void stop();
    void reset();

  private:
    size_t m_capacity;
    std::deque<OpkCommJob> m_jobs;
    std::mutex m_mutex;
    std::condition_variable m_jobsAvailable;
    bool m_stopped = false;
};

class Writer {
    _GstOpkComm *m_self;
    JobQueue m_queue;

    std::jthread m_worker;
    std::atomic<bool> m_running{false};

  protected:
    void run();

    virtual bool io_open() = 0;
    virtual void io_close() = 0;

    virtual bool publish(const std::string &json_str) = 0;

    const _GstOpkComm *self() const {
        return m_self;
    }

  public:
    explicit Writer(_GstOpkComm *self, size_t capacity) : m_self(self), m_queue(capacity) {}
    virtual ~Writer() = default;

    bool send(OpkCommJob &&job);

    bool check_running();

    virtual bool start();
    virtual void stop();
};
