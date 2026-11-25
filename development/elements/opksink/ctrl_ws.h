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

#ifndef __CTRL_WS_H__
#define __CTRL_WS_H__

#include <atomic>
#include <functional>
#include <glib.h>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unordered_map>

#define ASIO_STANDALONE
#include <asio.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtemplate-id-cdtor"

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#pragma GCC diagnostic pop

struct _GstOpkSink;

enum class CtrlSockerError {
    OK,
};

using ws_server = websocketpp::server<websocketpp::config::asio>;
using BrowserSessionId = websocketpp::connection_hdl;

class CtrlWebSocket {

    _GstOpkSink *self_;

    std::thread ws_server_thread;

    std::shared_ptr<ws_server> ws = nullptr;
    std::shared_ptr<GThreadPool> command_queue;
    std::shared_ptr<asio::steady_timer> report_timer;
    std::atomic_bool accepting_commands{false};
    std::atomic_uint pending_commands{0};
    mutable std::mutex command_status_mutex;
    std::string command_error;
    std::string last_report;

    void schedule_report();
    void enqueue_command(const BrowserSessionId &browserSessionId, const nlohmann::json &message);
    void execute_command(const BrowserSessionId &browserSessionId, const nlohmann::json &message);
    void handle_edit_session(const BrowserSessionId &browserSessionId, const bool active);
    void clear_command_error();
    static void process_command(gpointer data, gpointer user_data);

    std::unordered_map<std::string, std::function<void(const nlohmann::json &)>> command_handlers;

    std::set<BrowserSessionId, std::owner_less<BrowserSessionId>> browserSessionIds;
    BrowserSessionId edit_owner;

    mutable std::mutex reporter_lock;
    mutable std::mutex browserSessionsMutex;

    void set_pipeline_state(const nlohmann::json &message);
    void refresh_available_models(const nlohmann::json &message);
    void edit_models(const nlohmann::json &message);
    void model_toggle(const nlohmann::json &jsn);
    void model_inputs(const nlohmann::json &jsn);

    void send_to_all(const std::string &text);
    void send_to(const BrowserSessionId &browserSessionId, const nlohmann::json &message);

    void on_open(const BrowserSessionId &browserSessionId);
    void on_close(const BrowserSessionId &browserSessionId);
    void on_message(const BrowserSessionId &browserSessionId, const ws_server::message_ptr &msg);

    CtrlSockerError setup();

  public:
    CtrlWebSocket() = default;
    CtrlWebSocket(_GstOpkSink *self);

    CtrlSockerError start();
    CtrlSockerError stop();

    void report();
    bool available() const;
    void record_error(const std::string &message);
};

#endif // !__CTRL_WS_H__
