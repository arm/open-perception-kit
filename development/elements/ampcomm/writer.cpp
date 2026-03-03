/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "writer.h"

#include <unistd.h>
#include <utility>

#include <amp/Perception.h>
#include <amp/PerceptionSerializer.h>
#include <gst/PerceptionContextMeta.h>

#include <nlohmann/json.hpp>

bool JobQueue::try_push(AmpCommJob &&j) {

    std::lock_guard<std::mutex> lk(m_);

    if (stop_ || q_.size() >= max_)
        return false;

    q_.emplace_back(std::move(j));

    cv_.notify_one();
    return true;
}

bool JobQueue::pop(AmpCommJob &out) {
    std::unique_lock<std::mutex> lk(m_);

    cv_.wait(lk, [&] { return stop_ || !q_.empty(); });

    if (q_.empty())
        return false;

    out = std::move(q_.front());

    q_.pop_front();

    return true;
}

void JobQueue::stop() {
    std::lock_guard<std::mutex> lk(m_);
    stop_ = true;
    cv_.notify_all();
}

void JobQueue::reset() {
    std::lock_guard<std::mutex> lk(m_);
    stop_ = false;
}

bool Writer::start() {
    auto ret = false;
    if (!running) {
        q.reset();
        ret = io_open();

        if (ret) {
            running = true;
            th = std::thread([this] { run(); });
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

    AmpCommJob job;
    while (q.pop(job)) {

        json j;

        if (job.perception) {
            j["frame_counter"] = job.frame_counter;
            j["perception"] = *job.perception; // calls your to_json overloads
        } else {
            j["perception"] = nullptr;
        }

        std::string line = j.dump();
        line.push_back('\n');

        publish(line);
    }
}

bool Writer::send(AmpCommJob &&job) {
    auto ret = false;
    if (running) {
        ret = q.try_push(std::move(job));
    } else {
        ret = false;
    }

    return ret;
}
