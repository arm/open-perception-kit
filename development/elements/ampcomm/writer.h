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

#include <amp/Perception.h>

struct _GstAmpComm;

struct AmpCommJob {
    uint64_t frame_counter = 0;
    std::shared_ptr<const amp::Perception> perception;
};

class JobQueue {
  public:
    explicit JobQueue(size_t max) : max_(max) {}

    bool try_push(AmpCommJob &&j);
    bool pop(AmpCommJob &out);

    void stop();
    void reset();

  private:
    size_t max_;
    std::deque<AmpCommJob> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
};

class Writer {
    _GstAmpComm *m_self;
    JobQueue q;

    std::thread th;
    std::atomic<bool> running{false};

  protected:
    void run();

    virtual bool io_open() = 0;
    virtual void io_close() = 0;

    virtual bool publish(const std::string &json_str) = 0;

    const _GstAmpComm *self() {
        return m_self;
    }

  public:
    explicit Writer(_GstAmpComm *self, size_t max) : m_self(self), q(max) {}

    bool send(AmpCommJob &&job);

    virtual bool start();
    virtual void stop();
};
