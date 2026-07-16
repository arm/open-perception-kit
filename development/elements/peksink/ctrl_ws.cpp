/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "glib.h"
#include "gst/gstelement.h"
#include <functional>
#include <nlohmann/json_fwd.hpp>

#include <mutex>
#include <unordered_map>
#include <utility>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "auxiliary.h"
#include "ctrl_ws.h"
#include "pek/Log.h"
#include "peksink.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

using namespace nlohmann;

namespace {

std::string lower_copy(const std::string &value) {
    std::string ret = value;
    std::transform(ret.begin(), ret.end(), ret.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ret;
}

bool model_name_matches_dependency(const std::string &name, const std::string &token) {
    const auto lowered = lower_copy(name);

    if (lowered == token) {
        return true;
    }

    if (lowered.rfind(token, 0) != 0 || lowered.size() <= token.size()) {
        return false;
    }

    const char separator = lowered[token.size()];
    return separator == ' ' || separator == '-' || separator == '_';
}

std::vector<std::string> dependency_tokens_for_model(const std::string &model_name) {
    const auto lowered = lower_copy(model_name);

    if (lowered.find("cameracontact") != std::string::npos ||
        lowered.find("gazedetection") != std::string::npos) {
        return {"ultraface"};
    }

    if (lowered.find("osnetx025reid") != std::string::npos) {
        return {"yolov11"};
    }

    return {};
}

bool visible_model_active(const std::vector<ModelStatus> &statuses,
                          const std::string &element_name,
                          bool &active) {
    for (const auto &status : statuses) {
        if (status.element_name == element_name) {
            active = status.active;
            return true;
        }
    }

    return false;
}

void set_model_element_active(GstElement *pipeline,
                              const std::string &element_name,
                              bool active,
                              _GstPekSink *self) {
    GstElement *element = get_element_by_name(GST_ELEMENT(pipeline), element_name);
    if (!element) {
        GST_WARNING_OBJECT(self, "Element not found: %s", element_name.c_str());
        return;
    }

    g_object_set(element, "active", static_cast<gboolean>(active), NULL);
    gst_object_unref(element);
}

void apply_model_runtime_state(GstElement *pipeline,
                               const std::vector<ModelStatus> &statuses,
                               _GstPekSink *self) {
    std::set<std::string> required_elements;

    for (const auto &status : statuses) {
        if (!status.active) {
            continue;
        }

        for (const auto &dependency_token : dependency_tokens_for_model(status.name)) {
            for (const auto &candidate : statuses) {
                if (candidate.element_name != status.element_name &&
                    model_name_matches_dependency(candidate.name, dependency_token)) {
                    required_elements.insert(candidate.element_name);
                }
            }
        }
    }

    for (const auto &status : statuses) {
        const bool runtime_active =
            status.active || required_elements.find(status.element_name) != required_elements.end();
        set_model_element_active(pipeline, status.element_name, runtime_active, self);
        GST_INFO_OBJECT(self,
                        "Set element %s active=%d (visible=%d)",
                        status.element_name.c_str(),
                        runtime_active,
                        status.active);
    }
}

} // namespace

CtrlWebSocket::CtrlWebSocket(_GstPekSink *self) : self_(self) {
    using namespace std::placeholders;

    message_types = {
        {"play_pause", std::bind(&CtrlWebSocket::play_pause, this, _1)},
        {"perf_overlay", std::bind(&CtrlWebSocket::enable_perf_overlay, this, _1)},
        {"model_toggle", std::bind(&CtrlWebSocket::model_toggle, this, _1)},
        {"pipeline_restart", std::bind(&CtrlWebSocket::pipeline_restart, this, _1)},
        {"pipeline_switch", std::bind(&CtrlWebSocket::pipeline_switch, this, _1)},
    };
}

CtrlSockerError CtrlWebSocket::setup() {
    ws = std::make_shared<ws_server>();

    ws->init_asio();

    ws->set_open_handler([this](const connection_hdl &hdl) { on_open(hdl); });
    ws->set_close_handler([this](const connection_hdl &hdl) { on_close(hdl); });
    ws->set_message_handler([this](const connection_hdl &hdl, const ws_server::message_ptr &msg) {
        on_message(hdl, msg);
    });

    ws->set_reuse_addr(true);
    ws->listen(self_->ctrl_port);
    ws->start_accept();

    DBG("WebSocket server started");

    return CtrlSockerError::OK;
}

CtrlSockerError CtrlWebSocket::start() {
    using namespace std::chrono_literals;

    if (auto error = setup(); error != CtrlSockerError::OK) {
        return error;
    }
    DBG("WebSocket++ server listening on port {}", self_->ctrl_port);

    ws_server_thread = std::thread(&ws_server::run, ws);
    while (!ws->is_listening()) {
        std::this_thread::sleep_for(1ms);
    }

    return CtrlSockerError::OK;
}

CtrlSockerError CtrlWebSocket::stop() {
    if (ws) {
        ws->stop();
    }

    if (ws_server_thread.joinable()) {
        ws_server_thread.join();
    }

    return CtrlSockerError::OK;
}

void CtrlWebSocket::send_to_all(const std::string &text) {
    std::lock_guard<std::mutex> g(hdl_lock);

    for (auto it = hdls.begin(); it != hdls.end();) {
        websocketpp::lib::error_code ec;
        ws->send(*it, text, websocketpp::frame::opcode::text, ec);
        if (ec) {
            it = hdls.erase(it);
        } else {
            ++it;
        }
    }
}

void CtrlWebSocket::on_open(const connection_hdl &hdl) {
    {
        std::lock_guard<std::mutex> g(hdl_lock);
        hdls.insert(hdl);
    }

    report();
}

void CtrlWebSocket::on_close(const connection_hdl &hdl) {
    DBG("on_close");

    std::lock_guard<std::mutex> g(hdl_lock);
    hdls.erase(hdl);
}

void CtrlWebSocket::on_message(const connection_hdl &hdl, const ws_server::message_ptr &msg) {
    DBG("on_message");
    auto payload = msg->get_payload();
    auto jsn = json::parse(payload);
    auto type = jsn["type"].get<std::string>();

    if (auto it = message_types.find(type); it != message_types.end()) {
        (*it).second(jsn);
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
    DBG("reporting");

    std::lock_guard<std::mutex> g(reporter_lock);

    for (auto kv : status_reporters) {
        auto [name, reporter] = kv;

        auto report = reporter->report();
        rep[name] = report;

        if (name == "perception_data" && report.is_object()) {
            if (report.contains("performance")) {
                rep["performance"] = report["performance"];
            }
            if (report.contains("inference_output")) {
                rep["inference_output"] = report["inference_output"];
            }
        }
    }

    send_to_all(rep.dump());
    DBG("reported: {}", rep.dump());
}

struct ToggleStateRequest {
    GstElement *element = nullptr;
    GstState resulting = GST_STATE_NULL;

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
};

struct ToggleInvokeBox {
    std::shared_ptr<ToggleStateRequest> req;
};

struct RestartStateRequest {
    GstElement *element = nullptr;

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
};

struct RestartInvokeBox {
    std::shared_ptr<RestartStateRequest> req;
};

gboolean toggle_on_main(gpointer user_data) {

    pek::log("invoked\n");
    auto *box = static_cast<ToggleInvokeBox *>(user_data);
    auto tsr = box->req; // copy shared_ptr

    GstState cur = GST_STATE_NULL, pending = GST_STATE_NULL;
    gst_element_get_state(tsr->element, &cur, &pending, 0);

    // Decide target more robustly (treat "pending PLAYING" as playing)
    const bool is_playingish = (cur == GST_STATE_PLAYING) || (pending == GST_STATE_PLAYING);
    const GstState target = is_playingish ? GST_STATE_PAUSED : GST_STATE_PLAYING;

    gst_element_set_state(tsr->element, target);

    // Optionally wait a bit for the state to settle
    GstState after = GST_STATE_NULL, after_pending = GST_STATE_NULL;
    gst_element_get_state(tsr->element, &after, &after_pending, 200 * GST_MSECOND);

    {
        std::lock_guard<std::mutex> lk(tsr->m);
        tsr->resulting = after; // <- write under lock
        tsr->ok = true;
        tsr->done = true;
    }
    tsr->cv.notify_one();

    pek::log("check is_pipeline\n");

    // GST_IS_PIPELINE() is a macro performing a type check with no side effects
    if (tsr->element && GST_IS_PIPELINE(tsr->element)) { // NOSONAR
        pek::log("is_pipeline\n");
        gst_object_unref(tsr->element);
        tsr->element = nullptr;
    }

    return G_SOURCE_REMOVE;
}

void destroy_box(gpointer user_data) {
    delete static_cast<ToggleInvokeBox *>(user_data);
}

gboolean restart_on_main(gpointer user_data) {
    auto *box = static_cast<RestartInvokeBox *>(user_data);
    auto req = box->req;

    bool ok = false;
    if (req->element) {
        auto ready_ret = gst_element_set_state(req->element, GST_STATE_READY);
        gst_element_get_state(req->element, nullptr, nullptr, 3 * GST_SECOND);

        auto playing_ret = gst_element_set_state(req->element, GST_STATE_PLAYING);
        gst_element_get_state(req->element, nullptr, nullptr, 3 * GST_SECOND);

        ok = ready_ret != GST_STATE_CHANGE_FAILURE && playing_ret != GST_STATE_CHANGE_FAILURE;
    }

    {
        std::lock_guard<std::mutex> lk(req->m);
        req->ok = ok;
        req->done = true;
    }
    req->cv.notify_one();

    if (req->element && GST_IS_PIPELINE(req->element)) { // NOSONAR
        gst_object_unref(req->element);
        req->element = nullptr;
    }

    return G_SOURCE_REMOVE;
}

void destroy_restart_box(gpointer user_data) {
    delete static_cast<RestartInvokeBox *>(user_data);
}

// handle the play button presses on the html frontend
void CtrlWebSocket::play_pause(const json &jsn) {
    DBG("play-pause: {}", jsn.dump());

    auto tsr = std::make_shared<ToggleStateRequest>();

    GstElement *pipeline = get_top_pipeline(GST_ELEMENT(self_));
    tsr->element = pipeline ? pipeline : GST_ELEMENT(self_); // if pipeline, ref is held

    auto *box = new ToggleInvokeBox{tsr};
    g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, toggle_on_main, box, destroy_box);

    {
        std::unique_lock<std::mutex> lk(tsr->m);
        tsr->cv.wait_for(lk, std::chrono::milliseconds(800), [&] { return tsr->done; });
    }

    // send the current pipeline state back to browser
    report();
}

// handle to "enable/disable performance overlay" button presses on the html frontend
void CtrlWebSocket::enable_perf_overlay(const json &jsn) {
    DBG("enable_perf_overlay: {}", jsn.dump());

    auto top = get_top_pipeline(GST_ELEMENT(self_));
    auto perf_ovr = get_element_by_type(top, "pekperformance");
    gst_object_unref(top);

    // GST_IS_ELEMENT() is a macro performing a type check with no side effects
    if (perf_ovr && GST_IS_ELEMENT(perf_ovr)) { // NOSONAR

        gboolean enabled;
        g_object_get(perf_ovr, "enabled", &enabled, NULL);
        gst_object_unref(perf_ovr);

        GstStructure *structure =
            gst_structure_new("pekperformance", "enabled", G_TYPE_BOOLEAN, !enabled, NULL);
        GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, structure);

        // Get the peer pad (source pad of upstream element connected to our sink)
        GstPad *sink_pad = gst_element_get_static_pad(GST_ELEMENT(self_), "sink");
        if (sink_pad) {
            GstPad *peer_pad = gst_pad_get_peer(sink_pad);

            if (peer_pad) {
                GstElement *peer_elem = GST_ELEMENT(gst_pad_get_parent(peer_pad));
                GST_INFO_OBJECT(self_,
                                "Sending event to peer element: %s",
                                peer_elem ? GST_ELEMENT_NAME(peer_elem) : "unknown");

                gboolean result = gst_pad_send_event(peer_pad, event);

                if (peer_elem) {
                    gst_object_unref(peer_elem);
                }

                gst_object_unref(peer_pad);
            }

            gst_object_unref(sink_pad);
        }
    }

    // send the current pipeline state back to browser
    report();
}

void CtrlWebSocket::model_toggle(const json &jsn) {
    DBG("model_toggle: {}", jsn.dump());

    try {
        std::string element_name = jsn["name"];

        if (element_name.empty()) {
            GST_ERROR_OBJECT(self_, "element_name can't be empty");
            return;
        }

        GstElement *pipeline = get_top_pipeline(GST_ELEMENT(self_));
        if (!pipeline) {
            GST_ERROR_OBJECT(self_, "Could not get pipeline");
            return;
        }

        GstElement *target_element = get_element_by_name(GST_ELEMENT(pipeline), element_name);
        if (!target_element) {
            GST_WARNING_OBJECT(self_, "Element not found: %s", element_name.c_str());
            gst_object_unref(pipeline);
            return;
        }

        bool active = false;
        bool has_requested_active = false;
        if (jsn.contains("active") && jsn["active"].is_boolean()) {
            active = jsn["active"].get<bool>();
            has_requested_active = true;
        }

        if (!has_requested_active) {
            const auto current_statuses = self_->private_data->model_registry->snapshot();
            if (!visible_model_active(current_statuses, element_name, active)) {
                gboolean runtime_active = false;
                g_object_get(target_element, "active", &runtime_active, NULL);
                active = static_cast<bool>(runtime_active);
            }

            active = !active;
        }

        gst_object_unref(target_element);

        self_->private_data->model_registry->toggle_model(element_name, active);
        const auto statuses = self_->private_data->model_registry->snapshot();
        apply_model_runtime_state(pipeline, statuses, self_);
        gst_object_unref(pipeline);

        GST_INFO_OBJECT(self_, "Set visible model %s active=%d", element_name.c_str(), active);

    } catch (const json::exception &e) {
        GST_ERROR_OBJECT(self_, "JSON parse error: %s", e.what());
    }
}

void CtrlWebSocket::pipeline_restart(const json &jsn) {
    DBG("pipeline_restart: {}", jsn.dump());

    GstElement *pipeline = get_top_pipeline(GST_ELEMENT(self_));
    if (!pipeline) {
        GST_WARNING_OBJECT(self_,
                           "Pipeline restart requested, but no top-level pipeline was found");
        send_to_all(json{{"pipeline_restart",
                          {{"available", false},
                           {"requested", false},
                           {"message", "Pipeline restart is not available for this launch."}}}}
                        .dump());
        return;
    }

    send_to_all(
        json{{"pipeline_restart",
              {{"available", true}, {"requested", true}, {"message", "Restarting pipeline..."}}}}
            .dump());

    auto req = std::make_shared<RestartStateRequest>();
    req->element = pipeline;

    auto *box = new RestartInvokeBox{req};
    g_main_context_invoke_full(
        nullptr, G_PRIORITY_DEFAULT, restart_on_main, box, destroy_restart_box);

    {
        std::unique_lock<std::mutex> lk(req->m);
        req->cv.wait_for(lk, std::chrono::seconds(8), [&] { return req->done; });
    }

    send_to_all(json{
        {"pipeline_restart",
         {{"available", true},
          {"requested", false},
          {"complete", req->done && req->ok},
          {"message",
           req->done && req->ok ? "Pipeline restarted." : "Pipeline restart did not complete."}}}}
                    .dump());
    report();
}

void CtrlWebSocket::pipeline_switch(const json &jsn) {
    DBG("pipeline_switch: {}", jsn.dump());

    std::string requested_pipeline;
    try {
        if (jsn.contains("pipeline") && jsn["pipeline"].is_string()) {
            requested_pipeline = jsn["pipeline"].get<std::string>();
        }
    } catch (const json::exception &e) {
        GST_WARNING_OBJECT(self_, "Pipeline switch JSON parse error: %s", e.what());
    }

    send_to_all(
        json{{"pipeline_switch",
              {{"available", false},
               {"requested", false},
               {"pipeline", requested_pipeline},
               {"message",
                "Switching to another pipeline preset needs the WebUI supervisor launch mode."}}}}
            .dump());
}
