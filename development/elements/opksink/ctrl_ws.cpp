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
#include "model_manager.h"
#include "opksink.h"
#include "utils.h"

using namespace nlohmann;

namespace {
using json = nlohmann::json;

constexpr auto kConfigurationId = "configurationId";
constexpr auto kPosition = "position";
constexpr auto kProducerId = "producerId";
constexpr auto kName = "name";

template <typename Entry> json serializeInterfaceEntries(const std::vector<Entry> &outputs) {
    auto result = json::array();
    for (const auto &output : outputs) {
        json entry{{"payloadType", output.payloadType}, {kProducerId, output.producerId}};
        if constexpr (requires { output.consumerId; }) {
            entry["consumerId"] = output.consumerId;
            entry["inputId"] = output.inputId;
        }
        result.push_back(std::move(entry));
    }
    return result;
}

json serializeMetadata(const OpChainMetadata &metadata) {
    json result{{kName, metadata.m_name}};
    if (!metadata.m_displayName.empty()) {
        result["displayName"] = metadata.m_displayName;
    }
    if (!metadata.m_task.empty()) {
        result["task"] = metadata.m_task;
    }
    if (!metadata.m_runtime.empty()) {
        result["runtime"] = metadata.m_runtime;
    }
    return result;
}

json serializeModel(const Model &model, const std::string &groupId, const bool connected) {
    auto result = serializeMetadata(model.getMetadata());
    const auto &modelInterface = model.getInterface();
    result["element_name"] = model.getElementName();
    result["active"] = model.isActive();
    result["providedOutputs"] = serializeInterfaceEntries(modelInterface.m_providedOutputs);
    result["requiredInputs"] = serializeInterfaceEntries(model.getRequiredInputs());
    result["availableInputs"] = serializeInterfaceEntries(modelInterface.m_upstreamInputProviders);
    if (model.isConfigured()) {
        result["opchainPath"] = model.getOpChainPath();
        result["modelGroup"] = groupId;
        result["connected"] = connected;
    }
    return result;
}

ChangeAction readChangeAction(const std::string &action) {
    if (action == "add") {
        return ChangeAction::Add;
    }
    if (action == "replace") {
        return ChangeAction::Replace;
    }
    if (action == "remove") {
        return ChangeAction::Remove;
    }
    if (action == "move") {
        return ChangeAction::Move;
    }
    throw std::runtime_error("Unknown model edit");
}

ChangeRequest readChangeRequest(const json &message) {
    if (!message.at(kConfigurationId).is_number_unsigned()) {
        throw std::runtime_error("Invalid configuration ID");
    }
    const auto action = readChangeAction(message.at("action").get<std::string>());
    const bool needsPath = action == ChangeAction::Add || action == ChangeAction::Replace;
    std::optional<std::size_t> position;
    if (action == ChangeAction::Move ||
        (action == ChangeAction::Add && message.contains(kPosition))) {
        if (!message.at(kPosition).is_number_unsigned()) {
            throw std::runtime_error("Model position must be a non-negative integer");
        }
        position = message.at(kPosition).get<std::size_t>();
    }
    return {action,
            message.at(kConfigurationId).get<std::uint64_t>(),
            message.at("modelGroup").get<std::string>(),
            message.value(kName, std::string{}),
            needsPath ? message.at("path").get<std::string>() : "",
            position};
}

json serializeModels(const ModelGroupsSnapshot &snapshot) {
    auto result = json::array();
    for (const auto &group : snapshot.m_modelGroups) {
        const bool connected = group.isConnected();
        for (const auto &model : group.getModels()) {
            result.push_back(serializeModel(model, group.getId(), connected));
        }
    }
    return result;
}

json serializeModelConfiguration(const ModelGroupsSnapshot &snapshot,
                                 const std::map<std::string, OpChainMetadata> &availableModels) {
    auto groups = json::array();
    for (const auto &group : snapshot.m_modelGroups) {
        if (group.isEditable()) {
            groups.push_back({{"id", group.getId()}, {"connected", group.isConnected()}});
        }
    }
    auto choices = json::array();
    for (const auto &[path, metadata] : availableModels) {
        auto choice = serializeMetadata(metadata);
        choice["path"] = path;
        choices.push_back(std::move(choice));
    }
    return {{"modelGroups", groups},
            {"availableOpChains", choices},
            {kConfigurationId, snapshot.m_configurationId}};
}

} // namespace

static constexpr auto kChangePipelineStateRequestType = "change_pipeline_state";
static constexpr auto kModelEditSession = "model_edit_session";
static constexpr auto kControlError = "control_error";
static constexpr auto kPipelineAlreadyEditing = "This pipeline is being edited in another session.";

CtrlWebSocket::CtrlWebSocket(_GstOpkSink *self) : self_(self) {
    command_handlers = {
        {kChangePipelineStateRequestType,
         std::bind_front(&CtrlWebSocket::set_pipeline_state, this)},
        {"refresh_available_opchains",
         std::bind_front(&CtrlWebSocket::refresh_available_models, this)},
        {"model_edit", std::bind_front(&CtrlWebSocket::edit_models, this)},
        {"model_toggle", std::bind_front(&CtrlWebSocket::model_toggle, this)},
        {"model_inputs", std::bind_front(&CtrlWebSocket::model_inputs, this)},
    };
}

namespace {

constexpr auto kReportInterval = std::chrono::milliseconds(250);
constexpr std::size_t kMaxControlMessageBytes = std::size_t{1024} * 1024;
constexpr std::size_t kMaxElementNameBytes = 1024;
constexpr std::size_t kMaxInputBindings = 128;
constexpr std::size_t kMaxInputBindingsBytes = 65536;

struct ControlRequest {
    GstOpkSink *m_sink;
    std::weak_ptr<ws_server> m_server;
    BrowserSessionId m_browserSessionId;
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
            requestType == kChangePipelineStateRequestType || requestType == "model_edit" ||
            (requestType == kModelEditSession && request->m_message.value("active", false));
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
        control->execute_command(request->m_browserSessionId, request->m_message);
    } catch (const std::exception &error) {
        if (request->m_message.at("type") == kModelEditSession) {
            control->send_to(
                request->m_browserSessionId,
                {{"type", kModelEditSession}, {"active", false}, {"error", error.what()}});
        } else {
            control->record_error(error.what());
        }
    }
    {
        std::scoped_lock lock(control->browserSessionsMutex);
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

void CtrlWebSocket::enqueue_command(const BrowserSessionId &browserSessionId, const json &message) {
    std::scoped_lock lock(browserSessionsMutex);
    if (!accepting_commands || !command_queue) {
        return;
    }
    ++pending_commands;
    auto request = std::make_unique<ControlRequest>(
        reinterpret_cast<GstOpkSink *>(gst_object_ref(self_)), ws, browserSessionId, message);
    g_thread_pool_push(command_queue.get(), request.release(), nullptr);
}

void CtrlWebSocket::execute_command(const BrowserSessionId &browserSessionId, const json &message) {
    const auto type = message.at("type").get<std::string>();
    if (type == kModelEditSession) {
        handle_edit_session(browserSessionId, message.at("active").get<bool>());
        return;
    }
    std::string rejection;
    {
        std::scoped_lock lock(browserSessionsMutex);
        if (!browserSessionIds.contains(browserSessionId)) {
            // An unrecognized browser session tries to edit
            return;
        }
        const bool ownsEdit = !edit_owner.owner_before(browserSessionId) &&
                              !browserSessionId.owner_before(edit_owner);
        const bool editingInProgress = !edit_owner.expired();
        const bool pipelineIsEditedByAnotherSession = editingInProgress && !ownsEdit;
        const bool modelToggleOrInputSelectionCommandArrived =
            (type != "model_edit") && (type != "refresh_available_opchains");
        if (pipelineIsEditedByAnotherSession) {
            rejection = kPipelineAlreadyEditing;
        } else if (type == "model_edit" && !editingInProgress) {
            rejection = "Choose Edit Pipeline before changing models.";
        } else if (editingInProgress && modelToggleOrInputSelectionCommandArrived) {
            rejection = "Save or discard pipeline edits first.";
        }
    }
    if (!rejection.empty()) {
        send_to(browserSessionId, {{"type", kControlError}, {"error", rejection}});
        return;
    }
    clear_command_error();
    command_handlers.at(type)(message);
}

void CtrlWebSocket::handle_edit_session(const BrowserSessionId &browserSessionId,
                                        const bool active) {
    if (active) {
        g_autoptr(GstElement) pipeline = get_top_pipeline(GST_ELEMENT(self_));
        GstState current = GST_STATE_NULL;
        GstState pending = GST_STATE_VOID_PENDING;
        if (pipeline) {
            gst_element_get_state(pipeline, &current, &pending, 0);
        }
        if (current != GST_STATE_READY || pending != GST_STATE_VOID_PENDING) {
            throw std::runtime_error("Stop the pipeline before editing models");
        }
    }
    {
        std::scoped_lock lock(browserSessionsMutex);
        if (!browserSessionIds.contains(browserSessionId)) {
            return;
        }
        const bool ownsEdit = !edit_owner.owner_before(browserSessionId) &&
                              !browserSessionId.owner_before(edit_owner);
        if (!active) {
            if (ownsEdit) {
                edit_owner.reset();
            }
            return;
        }
        if (!edit_owner.expired() && !ownsEdit) {
            throw std::runtime_error(kPipelineAlreadyEditing);
        }
        edit_owner = browserSessionId;
    }
    clear_command_error();
    send_to(browserSessionId, {{"type", kModelEditSession}, {"active", true}, {"error", ""}});
}

bool CtrlWebSocket::available() const {
    return accepting_commands;
}

void CtrlWebSocket::record_error(const std::string &message) {
    std::scoped_lock lock(command_status_mutex);
    command_error = message;
}

void CtrlWebSocket::clear_command_error() {
    std::scoped_lock lock(command_status_mutex);
    command_error.clear();
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
    std::scoped_lock lock(browserSessionsMutex);
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
    edit_owner.reset();

    ws->init_asio();
    ws->clear_access_channels(websocketpp::log::alevel::all);
    ws->clear_error_channels(websocketpp::log::elevel::all);
    ws->set_error_channels(websocketpp::log::elevel::fatal);

    ws->set_open_handler(
        [this](const BrowserSessionId &browserSessionId) { on_open(browserSessionId); });
    ws->set_close_handler(
        [this](const BrowserSessionId &browserSessionId) { on_close(browserSessionId); });
    ws->set_message_handler(
        [this](const BrowserSessionId &browserSessionId, const ws_server::message_ptr &msg) {
            on_message(browserSessionId, msg);
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
            std::scoped_lock lock(browserSessionsMutex);
            for (const auto &browserSessionId : browserSessionIds) {
                ws->close(browserSessionId, websocketpp::close::status::going_away, "", ec);
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
    std::scoped_lock lock(browserSessionsMutex);
    browserSessionIds.clear();
    edit_owner.reset();
    ws.reset();

    return CtrlSockerError::OK;
}

void CtrlWebSocket::send_to_all(const std::string &text) {
    std::scoped_lock lock(browserSessionsMutex);

    if (!ws) {
        return;
    }

    for (const auto &browserSessionId : browserSessionIds) {
        websocketpp::lib::error_code ec;
        ws->send(browserSessionId, text, websocketpp::frame::opcode::text, ec);
    }
}

void CtrlWebSocket::send_to(const BrowserSessionId &browserSessionId, const json &message) {
    std::scoped_lock lock(browserSessionsMutex);
    if (ws && browserSessionIds.contains(browserSessionId)) {
        websocketpp::lib::error_code error;
        ws->send(browserSessionId, message.dump(), websocketpp::frame::opcode::text, error);
    }
}

void CtrlWebSocket::on_open(const BrowserSessionId &browserSessionId) {
    // A handshake accepted before stop_listening() can complete during shutdown.
    if (!ws->is_listening()) {
        websocketpp::lib::error_code ec;
        ws->close(browserSessionId, websocketpp::close::status::going_away, "", ec);
        return;
    }
    {
        std::scoped_lock lock(browserSessionsMutex);
        browserSessionIds.insert(browserSessionId);
    }

    {
        std::scoped_lock lock(reporter_lock);
        last_report.clear();
    }
    report();
}

void CtrlWebSocket::on_close(const BrowserSessionId &browserSessionId) {
    opk::log::debug("on_close");

    std::scoped_lock lock(browserSessionsMutex);
    browserSessionIds.erase(browserSessionId);
    if (!edit_owner.owner_before(browserSessionId) && !browserSessionId.owner_before(edit_owner)) {
        edit_owner.reset();
    }
}

void CtrlWebSocket::on_message(const BrowserSessionId &browserSessionId,
                               const ws_server::message_ptr &msg) {
    opk::log::debug("on_message");
    try {
        auto payload = msg->get_payload();
        auto jsn = json::parse(payload);
        auto type = jsn.at("type").get<std::string>();

        if (type == kModelEditSession || command_handlers.contains(type)) {
            enqueue_command(browserSessionId, jsn);
        }
    } catch (const json::exception &) {
        opk::log::debug("Dropping invalid control message");
    }
}

void CtrlWebSocket::report() {
    {
        std::scoped_lock lock(browserSessionsMutex);
        if (browserSessionIds.empty()) {
            return;
        }
    }
    nlohmann::json rep;

    std::scoped_lock lock(reporter_lock);

    rep["pipeline_state"] = self_->private_data->pipeline_state_reporter->report();
    const auto &manager = self_->private_data->modelManager;
    const auto snapshot = manager->getModelGroups();
    rep["models"] = serializeModels(snapshot);
    rep["model_editor"] = serializeModelConfiguration(snapshot, manager->getAvailableModels());
    {
        std::scoped_lock lock(browserSessionsMutex);
        rep["model_editor"]["editing"] = !edit_owner.expired();
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

void CtrlWebSocket::refresh_available_models(const json & /*message*/) {
    self_->private_data->modelManager->refreshAvailableModels();
}

void CtrlWebSocket::edit_models(const json &message) {
    g_autoptr(GstElement) pipeline = get_top_pipeline(GST_ELEMENT(self_));
    if (!pipeline) {
        throw std::runtime_error("Pipeline is unavailable");
    }
    self_->private_data->modelManager->edit(pipeline, readChangeRequest(message));
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
