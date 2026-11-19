/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <gst/gst.h>
#include <nlohmann/json.hpp>

// Public GStreamer prompt protocol adapter; no model or private metadata dependency.
class PromptControl : public std::enable_shared_from_this<PromptControl> {
  public:
    using Post = std::function<void(std::function<void()>)>;
    using Notify = std::function<void(const std::string &, const nlohmann::json &, bool)>;

    PromptControl(GstElement *const sink, Post post, Notify notify, Post cleanup = {});
    ~PromptControl();

    void start();
    void stop();
    nlohmann::json open();
    void close(const std::string &session);
    nlohmann::json command(const std::string &session, const nlohmann::json &message);

    static constexpr size_t MaxNameBytes = 128;
    static constexpr size_t MaxMessageBytes = 16 * 1024;
    static constexpr size_t MaxPendingNotifications = 64;
    static constexpr size_t MaxSocketBacklogBytes = MaxMessageBytes * MaxPendingNotifications;

  private:
    struct Target;
    struct Session;
    struct Capture;

    static thread_local Capture *capture_;
    static void busMessage(GstBus *const bus, GstMessage *const message, gpointer data);
    void observe(GstMessage *const message);
    std::map<std::string, Target> targets() const;
    nlohmann::json dispatch(const std::shared_ptr<Session> &session,
                            const std::string &commandId,
                            const std::string &targetName,
                            const std::string &operation,
                            const nlohmann::json &message);

    mutable GWeakRef sink_;
    GstBus *bus_ = nullptr;
    gulong observer_ = 0;
    Post post_;
    Notify notify_;
    Post cleanup_;
    std::atomic<bool> active_{false};
    std::mutex sessionsMutex_;
    std::map<std::string, std::shared_ptr<Session>> sessions_;
};
