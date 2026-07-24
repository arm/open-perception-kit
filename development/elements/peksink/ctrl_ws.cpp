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

#include "Log.h"
#include "auxiliary.h"
#include "ctrl_ws.h"
#include "peksink.h"
#include "utils.h"

using namespace nlohmann;

CtrlWebSocket::CtrlWebSocket(_GstPekSink *self) : self_(self) {
    using namespace std::placeholders;

    message_types = {
        {"play_pause", std::bind(&CtrlWebSocket::play_pause, this, _1)},
        {"model_toggle", std::bind(&CtrlWebSocket::model_toggle, this, _1)},
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

        rep[name] = reporter->report();
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

gboolean toggle_on_main(gpointer user_data) {

    pek::log::info("invoked\n");
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

    pek::log::info("check is_pipeline\n");

    // GST_IS_PIPELINE() is a macro performing a type check with no side effects
    if (tsr->element && GST_IS_PIPELINE(tsr->element)) { // NOSONAR
        pek::log::info("is_pipeline\n");
        gst_object_unref(tsr->element);
        tsr->element = nullptr;
    }

    return G_SOURCE_REMOVE;
}

void destroy_box(gpointer user_data) {
    delete static_cast<ToggleInvokeBox *>(user_data);
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

void CtrlWebSocket::model_toggle(const json &jsn) {
    DBG("model_toggle: {}", jsn.dump());

    try {
        std::string element_name = jsn["name"];

        if (element_name.empty()) {
            GST_ERROR_OBJECT(self_, "element_name can't be empty");
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
            GST_WARNING_OBJECT(self_, "Element not found: %s", element_name.c_str());
            return;
        }

        gboolean active;
        g_object_get(target_element, "active", &active, NULL);
        active = !active;
        g_object_set(target_element, "active", active, NULL);
        gst_object_unref(target_element);

        self_->private_data->model_registry->toggle_model(element_name, active);

        GST_INFO_OBJECT(self_, "Set element %s active=%d", element_name.c_str(), active);

    } catch (const json::exception &e) {
        GST_ERROR_OBJECT(self_, "JSON parse error: %s", e.what());
    }
}
