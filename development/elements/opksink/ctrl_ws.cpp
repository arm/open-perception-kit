/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "glib.h"
#include "gst/gstelement.h"
#include <condition_variable>
#include <functional>
#include <nlohmann/json_fwd.hpp>

#include <mutex>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "Log.h"
#include "ctrl_ws.h"
#include "opksink.h"
#include "utils.h"

using namespace nlohmann;

CtrlWebSocket::CtrlWebSocket(_GstOpkSink *self) : self_(self) {
    using namespace std::placeholders;

    message_types = {
        {"play_pause", std::bind(&CtrlWebSocket::play_pause, this, _1)},
        {"model_toggle", std::bind(&CtrlWebSocket::model_toggle, this, _1)},
    };
}

CtrlSockerError CtrlWebSocket::setup() {
    std::lock_guard<std::mutex> g(hdl_lock);
    ws = std::make_shared<ws_server>();

    ws->init_asio();
    ws->clear_error_channels(websocketpp::log::elevel::all);
    ws->set_error_channels(websocketpp::log::elevel::fatal);

    ws->set_open_handler([this](const connection_hdl &hdl) { on_open(hdl); });
    ws->set_close_handler([this](const connection_hdl &hdl) { on_close(hdl); });
    ws->set_message_handler([this](const connection_hdl &hdl, const ws_server::message_ptr &msg) {
        on_message(hdl, msg);
    });

    ws->set_reuse_addr(true);
    ws->listen(self_->ctrl_port);
    ws->start_accept();

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
    if (ws) {
        ws->get_io_service().post([this] {
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
        auto type = jsn["type"].get<std::string>();

        if (auto it = message_types.find(type); it != message_types.end()) {
            (*it).second(jsn);
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
    opk::log::debug("reporting");

    std::lock_guard<std::mutex> g(reporter_lock);

    for (auto kv : status_reporters) {
        auto [name, reporter] = kv;

        rep[name] = reporter->report();
    }

    send_to_all(rep.dump());
    opk::log::debug("reported: {}", rep.dump());
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
    // Track the originating server without keeping it alive. After stop() joins
    // its worker and resets ws, expired() lets the callback discard this request
    // so a delayed play/pause command cannot restart a stopped pipeline.
    std::weak_ptr<ws_server> server;
};

static void toggle_state_async(GstElement *sink, gpointer user_data) {
    auto *box = static_cast<ToggleInvokeBox *>(user_data);
    auto request = box->req;

    // Keep the sink and its parents alive while applying the request.
    std::vector<GstElement *> state_elements{GST_ELEMENT(gst_object_ref(sink))};
    while (GstElement *parent = GST_ELEMENT(gst_element_get_parent(state_elements.back()))) {
        state_elements.push_back(parent);
        if (GST_IS_PIPELINE(parent)) { // NOSONAR
            break;
        }
    }
    request->element = GST_IS_PIPELINE(state_elements.back()) ? state_elements.back() : sink;

    // Lock pipeline -> bins -> sink so play/pause cannot race shutdown.
    for (GstElement *state_element : std::views::reverse(state_elements)) {
        GST_STATE_LOCK(state_element);
    }

    bool state_change_ok = false;
    // A queued command from a stopped server must not restart the pipeline.
    if (!box->server.expired()) {
        GstState current = GST_STATE_NULL, pending = GST_STATE_NULL;
        gst_element_get_state(request->element, &current, &pending, 0);
        const bool playing = current == GST_STATE_PLAYING || pending == GST_STATE_PLAYING;
        const GstState next_state = playing ? GST_STATE_PAUSED : GST_STATE_PLAYING;
        state_change_ok =
            gst_element_set_state(request->element, next_state) != GST_STATE_CHANGE_FAILURE;
    }

    for (GstElement *state_element : state_elements) {
        GST_STATE_UNLOCK(state_element);
    }

    // Wait without holding state locks, then release the references.
    GstState resulting = GST_STATE_NULL, pending = GST_STATE_NULL;
    if (state_change_ok) {
        gst_element_get_state(request->element, &resulting, &pending, 200 * GST_MSECOND);
    }
    request->element = nullptr;
    for (GstElement *state_element : state_elements) {
        gst_object_unref(state_element);
    }

    {
        std::lock_guard<std::mutex> lock(request->m);
        request->resulting = resulting;
        request->ok = state_change_ok;
        request->done = true;
    }
    request->cv.notify_one();
}

static void destroy_box(gpointer user_data) {
    delete static_cast<ToggleInvokeBox *>(user_data);
}

// handle the play button presses on the html frontend
void CtrlWebSocket::play_pause(const json &jsn) {
    opk::log::debug("play-pause: {}", jsn.dump());

    auto request = std::make_shared<ToggleStateRequest>();
    auto *box = new ToggleInvokeBox{request, ws};
    gst_element_call_async(GST_ELEMENT(self_), toggle_state_async, box, destroy_box);

    {
        std::unique_lock<std::mutex> lock(request->m);
        request->cv.wait_for(lock, std::chrono::milliseconds(800), [&] { return request->done; });
    }

    // send the current pipeline state back to browser
    report();
}

void CtrlWebSocket::model_toggle(const json &jsn) {
    opk::log::debug("model_toggle: {}", jsn.dump());

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
            GST_WARNING_OBJECT(
                self_, "Element not found: %s", opk::log::escape(element_name).c_str());
            return;
        }

        gboolean active;
        g_object_get(target_element, "active", &active, NULL);
        active = !active;
        g_object_set(target_element, "active", active, NULL);
        gst_object_unref(target_element);

        GST_INFO_OBJECT(
            self_, "Set element %s active=%d", opk::log::escape(element_name).c_str(), active);

    } catch (const json::exception &e) {
        GST_ERROR_OBJECT(self_, "JSON parse error: %s", opk::log::escape(e.what()).c_str());
    }
}
