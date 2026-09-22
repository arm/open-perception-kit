/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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
    std::shared_ptr<const perception::FrameResults> frameResults;
};

class JobQueue {
  public:
    explicit JobQueue(size_t max) : max_(max) {}

    bool try_push(OpkCommJob &&j);
    bool pop(OpkCommJob &out);

    void stop();
    void reset();

  private:
    size_t max_;
    std::deque<OpkCommJob> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
};

class Writer {
    _GstOpkComm *m_self;
    JobQueue q;

    std::jthread th;
    std::atomic<bool> running{false};

  protected:
    void run();

    virtual bool io_open() = 0;
    virtual void io_close() = 0;

    virtual bool publish(const std::string &json_str) = 0;

    const _GstOpkComm *self() const {
        return m_self;
    }

  public:
    explicit Writer(_GstOpkComm *self, size_t max) : m_self(self), q(max) {}
    virtual ~Writer() = default;

    bool send(OpkCommJob &&job);

    bool check_running();

    virtual bool start();
    virtual void stop();
};
