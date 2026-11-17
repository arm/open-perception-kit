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

#include "glib.h"
#include "gst/gstelement.h"
#include <chrono>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>

#include <mutex>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "Log.h"
#include "ctrl_ws.h"
#include "gst/PipelineControl.h"
#include "opksink.h"
#include "utils.h"

using namespace nlohmann;

static constexpr auto kChangePipelineStateRequestType = "change_pipeline_state";

CtrlWebSocket::CtrlWebSocket(_GstOpkSink *self) : self_(self) {
    using namespace std::placeholders;

    message_types = {
        {kChangePipelineStateRequestType,
         [this](const json &message) { set_pipeline_state(message); }},
        {"refresh_available_opchains",
         [this](const json &) { model_editor->refreshAvailableOpChains(); }},
        {"model_edit",
         [this](const json &message) {
             g_autoptr(GstElement) pipeline = get_top_pipeline(GST_ELEMENT(self_));
             if (!pipeline) {
                 throw std::runtime_error("Pipeline is unavailable");
             }
             model_editor->edit(pipeline, message, *self_->private_data->model_registry);
         }},
        {"model_toggle", std::bind(&CtrlWebSocket::model_toggle, this, _1)},
        {"model_inputs", [this](const json &message) { model_inputs(message); }},
    };
}

namespace {

constexpr auto kReportInterval = std::chrono::milliseconds(250);
constexpr std::size_t kMaxControlMessageBytes = 1024 * 1024;
constexpr std::size_t kMaxElementNameBytes = 1024;
constexpr std::size_t kMaxInputBindings = 128;
constexpr std::size_t kMaxInputBindingsBytes = 65536;

struct ControlRequest {
    GstOpkSink *m_sink;
    std::weak_ptr<ws_server> m_server;
    json m_message;

    ~ControlRequest() {
        gst_object_unref(m_sink);
    }
};

// Never wait for a state lock while shutdown may be joining the WebSocket thread.
class PipelineStateLock {
  public:
    explicit PipelineStateLock(GstElement *const sink) {
        m_elements.push_back(GST_ELEMENT(gst_object_ref(sink)));
        while (GstElement *const parent = GST_ELEMENT(gst_element_get_parent(m_elements.back()))) {
            m_elements.push_back(parent);
        }
        for (GstElement *const element : std::views::reverse(m_elements)) {
            if (!g_rec_mutex_trylock(GST_STATE_GET_LOCK(element))) {
                return;
            }
            ++m_lockedCount;
        }
    }

    PipelineStateLock(const PipelineStateLock &) = delete;
    PipelineStateLock &operator=(const PipelineStateLock &) = delete;

    ~PipelineStateLock() {
        for (std::size_t index = m_elements.size() - m_lockedCount; index < m_elements.size();
             ++index) {
            GST_STATE_UNLOCK(m_elements[index]);
        }
        for (GstElement *const element : m_elements) {
            gst_object_unref(element);
        }
    }

    bool acquired() const {
        return m_lockedCount == m_elements.size();
    }

  private:
    std::vector<GstElement *> m_elements;
    std::size_t m_lockedCount = 0;
};

} // namespace

void CtrlWebSocket::process_command(gpointer data, gpointer) {
    const std::unique_ptr<ControlRequest> request(static_cast<ControlRequest *>(data));
    auto *const control = request->m_sink->private_data->ctrl_websocket.get();
    if (request->m_server.expired() || !control->available()) {
        return;
    }
    try {
        const auto &requestType = request->m_message.at("type");
        const bool requiresStablePipeline =
            requestType == kChangePipelineStateRequestType || requestType == "model_edit";
        std::optional<PipelineStateLock> stateLock;
        if (requiresStablePipeline) {
            stateLock.emplace(GST_ELEMENT(request->m_sink));
            if (!stateLock->acquired()) {
                throw std::runtime_error("Pipeline is changing state; retry when it finishes");
            }
            if (request->m_server.expired() || !control->available()) {
                return;
            }
        }
        control->execute_command(request->m_message);
    } catch (const std::exception &error) {
        control->record_error(error.what());
    }
    {
        std::scoped_lock lock(control->hdl_lock);
        if (request->m_server.lock() != control->ws) {
            return;
        }
        --control->pending_commands;
    }
    {
        std::scoped_lock lock(control->reporter_lock);
        control->last_report.clear();
    }
    control->report();
}

void CtrlWebSocket::enqueue_command(const json &message) {
    std::scoped_lock lock(hdl_lock);
    if (!accepting_commands || !command_queue) {
        return;
    }
    ++pending_commands;
    auto request = std::make_unique<ControlRequest>(
        reinterpret_cast<GstOpkSink *>(gst_object_ref(self_)), ws, message);
    g_thread_pool_push(command_queue.get(), request.release(), nullptr);
}

void CtrlWebSocket::execute_command(const json &message) {
    {
        std::scoped_lock lock(command_status_mutex);
        command_error.clear();
    }
    message_types.at(message.at("type").get<std::string>())(message);
}

bool CtrlWebSocket::available() const {
    return accepting_commands;
}

void CtrlWebSocket::record_error(const std::string &message) {
    std::scoped_lock lock(command_status_mutex);
    command_error = message;
}

void CtrlWebSocket::schedule_report() {
    report_timer->expires_after(kReportInterval);
    report_timer->async_wait([this](const asio::error_code &error) {
        if (!error && accepting_commands) {
            report();
            schedule_report();
        }
    });
}

CtrlSockerError CtrlWebSocket::setup() {
    std::lock_guard<std::mutex> g(hdl_lock);
    if (!model_editor) {
        model_editor =
            std::make_unique<ModelEditor>(GST_ELEMENT(self_), *self_->private_data->model_registry);
    }
    ws = std::make_shared<ws_server>();
    constexpr int commandWorkerCount = 1;
    auto *const queue = g_thread_pool_new_full(
        process_command,
        nullptr,
        [](gpointer data) { delete static_cast<ControlRequest *>(data); },
        commandWorkerCount,
        FALSE,
        nullptr);
    command_queue.reset(queue, [](GThreadPool *const pool) {
        // A property callback may stop this server from the command worker.
        g_thread_pool_free(pool, TRUE, FALSE);
    });
    pending_commands = 0;

    ws->init_asio();
    ws->clear_access_channels(websocketpp::log::alevel::all);
    ws->clear_error_channels(websocketpp::log::elevel::all);
    ws->set_error_channels(websocketpp::log::elevel::fatal);

    ws->set_open_handler([this](const connection_hdl &hdl) { on_open(hdl); });
    ws->set_close_handler([this](const connection_hdl &hdl) { on_close(hdl); });
    ws->set_message_handler([this](const connection_hdl &hdl, const ws_server::message_ptr &msg) {
        on_message(hdl, msg);
    });

    ws->set_max_message_size(kMaxControlMessageBytes);
    ws->set_reuse_addr(true);
    ws->listen(self_->ctrl_port);
    ws->start_accept();
    accepting_commands = true;
    report_timer = std::make_shared<asio::steady_timer>(ws->get_io_service());
    schedule_report();

    opk::log::debug("WebSocket server started");

    return CtrlSockerError::OK;
}

CtrlSockerError CtrlWebSocket::start() {
    using namespace std::chrono_literals;

    if (auto error = setup(); error != CtrlSockerError::OK) {
        return error;
    }
    opk::log::debug("WebSocket++ server listening on port {}", self_->ctrl_port);

    ws_server_thread = std::thread(&ws_server::run, ws);
    while (!ws->is_listening()) {
        std::this_thread::sleep_for(1ms);
    }

    return CtrlSockerError::OK;
}

CtrlSockerError CtrlWebSocket::stop() {
    accepting_commands = false;
    if (ws) {
        ws->get_io_service().post([this] {
            if (report_timer) {
                report_timer->cancel();
            }
            websocketpp::lib::error_code ec;
            ws->stop_listening(ec);
            std::lock_guard<std::mutex> g(hdl_lock);
            for (const auto &hdl : hdls) {
                ws->close(hdl, websocketpp::close::status::going_away, "", ec);
            }
        });
    }

    if (ws_server_thread.joinable()) {
        ws_server_thread.join();
    } else if (ws) {
        ws->run(); // Startup may have failed before the worker was created.
    }

    command_queue.reset();
    report_timer.reset();
    std::lock_guard<std::mutex> g(hdl_lock);
    hdls.clear();
    ws.reset();

    return CtrlSockerError::OK;
}

void CtrlWebSocket::send_to_all(const std::string &text) {
    std::lock_guard<std::mutex> g(hdl_lock);

    if (!ws) {
        return;
    }

    for (const auto &hdl : hdls) {
        websocketpp::lib::error_code ec;
        ws->send(hdl, text, websocketpp::frame::opcode::text, ec);
    }
}

void CtrlWebSocket::on_open(const connection_hdl &hdl) {
    // A handshake accepted before stop_listening() can complete during shutdown.
    if (!ws->is_listening()) {
        websocketpp::lib::error_code ec;
        ws->close(hdl, websocketpp::close::status::going_away, "", ec);
        return;
    }
    {
        std::lock_guard<std::mutex> g(hdl_lock);
        hdls.insert(hdl);
    }

    {
        std::scoped_lock lock(reporter_lock);
        last_report.clear();
    }
    report();
}

void CtrlWebSocket::on_close(const connection_hdl &hdl) {
    opk::log::debug("on_close");

    std::lock_guard<std::mutex> g(hdl_lock);
    hdls.erase(hdl);
}

void CtrlWebSocket::on_message(const connection_hdl &hdl, const ws_server::message_ptr &msg) {
    opk::log::debug("on_message");
    try {
        auto payload = msg->get_payload();
        auto jsn = json::parse(payload);
        auto type = jsn.at("type").get<std::string>();

        if (auto it = message_types.find(type); it != message_types.end()) {
            enqueue_command(jsn);
        }
    } catch (const json::exception &) {
        opk::log::debug("Dropping invalid control message");
    }
}

void CtrlWebSocket::register_status_reporter(const std::string &name,
                                             std::shared_ptr<StatusReporter> status_reporter) {
    std::lock_guard<std::mutex> g(reporter_lock);

    status_reporter->trigger_reporting = std::bind(&CtrlWebSocket::report, this);
    status_reporters[name] = std::move(status_reporter);
}

void CtrlWebSocket::report() {
    nlohmann::json rep;

    std::lock_guard<std::mutex> g(reporter_lock);

    for (const auto &[name, reporter] : status_reporters) {
        const bool editorReportsModels =
            model_editor && reporter == self_->private_data->model_registry;
        if (!editorReportsModels) {
            rep[name] = reporter->report();
        }
    }

    if (model_editor) {
        const auto snapshot = model_editor->statusOf(*self_->private_data->model_registry);
        rep["models"] = snapshot.at("models");
        rep["model_editor"] = snapshot.at("editor");
    }
    {
        std::scoped_lock lock(command_status_mutex);
        rep["pipeline_state"]["error"] = command_error;
    }
    rep["pipeline_state"]["busy"] = pending_commands.load() != 0;
    const auto encoded = rep.dump();
    if (encoded != last_report) {
        last_report = encoded;
        send_to_all(encoded);
    }
}

void CtrlWebSocket::set_pipeline_state(const json &message) {
    const auto requested = message.at("state").get<std::string>();
    const std::unordered_map<std::string, GstState> states{
        {"PLAYING", GST_STATE_PLAYING}, {"PAUSED", GST_STATE_PAUSED}, {"READY", GST_STATE_READY}};
    const auto target = states.find(requested);
    if (target == states.end()) {
        throw std::runtime_error("Use Play, Pause or Stop to change pipeline state");
    }
    g_autoptr(GstElement) pipeline = get_top_pipeline(GST_ELEMENT(self_));
    if (!pipeline) {
        throw std::runtime_error("Pipeline is unavailable");
    }
    if (gst::pipeline_control::setState(pipeline, target->second) == GST_STATE_CHANGE_FAILURE) {
        gst::pipeline_control::setState(pipeline, GST_STATE_READY);
        throw std::runtime_error("Pipeline could not start; correct the configuration and retry");
    }
}

void CtrlWebSocket::model_toggle(const json &jsn) {
    opk::log::debug("model_toggle: {}", jsn.dump());

    try {
        auto element_name = jsn.at("name").get<std::string>();

        // TODO(EXPKITS-1382): Revisit when control message schema validation is enabled.
        if (element_name.empty() || element_name.find('\0') != std::string::npos) {
            opk::log::debug("Dropping invalid model toggle name");
            return;
        }

        // Get the pipeline
        GstElement *pipeline = get_top_pipeline(GST_ELEMENT(self_));
        if (!pipeline) {
            GST_ERROR_OBJECT(self_, "Could not get pipeline");
            return;
        }

        // Find the element by name
        GstElement *target_element = get_element_by_name(GST_ELEMENT(pipeline), element_name);
        gst_object_unref(pipeline);

        if (!target_element) {
            GST_WARNING_OBJECT(
                self_, "Element not found: %s", opk::log::escape(element_name).c_str());
            return;
        }

        const auto *const property =
            g_object_class_find_property(G_OBJECT_GET_CLASS(target_element), "active");
        if (property && G_PARAM_SPEC_VALUE_TYPE(property) == G_TYPE_BOOLEAN) {
            gboolean active = FALSE;
            g_object_get(target_element, "active", &active, nullptr);
            g_object_set(target_element, "active", !active, nullptr);
        }
        gst_object_unref(target_element);
    } catch (const json::exception &e) {
        opk::log::debug("Dropping invalid model toggle: {}", e.what());
    }
}

void CtrlWebSocket::model_inputs(const json &jsn) {
    try {
        const auto name = jsn.at("name").get<std::string>();
        const auto &bindings = jsn.at("bindings");
        if (name.empty() || name.size() > kMaxElementNameBytes ||
            name.find('\0') != std::string::npos || !bindings.is_array() ||
            bindings.size() > kMaxInputBindings)
            return;
        const auto encoded = bindings.dump();
        if (encoded.size() > kMaxInputBindingsBytes)
            return;
        GstElement *const pipeline = get_top_pipeline(GST_ELEMENT(self_));
        if (!pipeline)
            return;
        GstElement *const target = get_element_by_name(pipeline, name);
        gst_object_unref(pipeline);
        if (!target)
            return;
        const auto *const property =
            g_object_class_find_property(G_OBJECT_GET_CLASS(target), "input-bindings");
        if (property && G_PARAM_SPEC_VALUE_TYPE(property) == G_TYPE_STRING)
            g_object_set(target, "input-bindings", encoded.c_str(), nullptr);
        gst_object_unref(target);
    } catch (const json::exception &error) {
        opk::log::debug("Dropping invalid model input connections: {}", error.what());
    }
}
