
#include "http_server.h"
#include "ampsink.h"
#include "utils.h"

using namespace httplib;

static void get_dynamic_config(GstAmpSink *self, const Request &req, Response &res);
static void model_registry(GstAmpSink *self, const Request &req, Response &res);
static void model_toggle(GstAmpSink *self, const Request &req, Response &res);
static void ctrl(GstAmpSink *self, const Request &req, Response &res);
static void play_pause(GstAmpSink *self, const Request &req, Response &res);

void gst_amp_sink_setup_http_server(GstAmpSink *self) {

    auto &http_server = self->private_data->http_server;

    self->private_data->http_server = std::make_unique<Server>();

    // Dynamic config endpoint
    http_server->Get("/amp-config.js", [self](const Request &req, Response &res) {
        get_dynamic_config(self, req, res);
    });

    // Model registry endpoint
    http_server->Get("/models",
                     [self](const Request &req, Response &res) { model_registry(self, req, res); });

    // Model toggle endpoint
    http_server->Post("/models/toggle",
                      [self](const Request &req, Response &res) { model_toggle(self, req, res); });

    auto ret = http_server->set_mount_point("/", self->static_files_location);
    if (!ret) {
        // TODO@ibori: error handling
        throw std::runtime_error(
            std::string("Static file directory does not exist: ") +
            (self->static_files_location ? self->static_files_location : "(null)"));
    }

    http_server->Post("/ctrl", [self](const Request &req, Response &res) { ctrl(self, req, res); });

    http_server->Post("/play-pause",
                      [self](const Request &req, Response &res) { play_pause(self, req, res); });

    // TODO@ibori: error handling
    auto started = http_server->listen(self->host, self->http_port);
    if (!started) {
        // TODO@ibori: error handling
        throw std::runtime_error(std::string("HTTP server failed to start on ") +
                                 std::string(self->host ? self->host : "<null>") + ":" +
                                 std::to_string(self->http_port));
    }
}

static void get_dynamic_config(GstAmpSink *self, const Request &req, Response &res) {

    std::string js = "window.AMP_CONFIG = { wsPort: " + std::to_string(self->ws_port) + " };";
    res.set_content(js, "application/javascript");
}

static void model_registry(GstAmpSink *self, const Request &req, Response &res) {

    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");

    json models_array = json::array();

    {
        std::lock_guard<std::mutex> lock(self->private_data->model_registry_mutex);
        for (const auto &entry : self->private_data->model_registry) {
            json model_obj = {{"element_name", entry.second.element_name},
                              {"model_name", entry.second.name},
                              {"active", entry.second.active}};
            models_array.push_back(model_obj);
        }
    }

    json response = {{"models", models_array}, {"count", models_array.size()}};

    res.set_content(response.dump(2), "application/json");
}

static void model_toggle(GstAmpSink *self, const Request &req, Response &res) {

    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");

    try {
        json request_data = json::parse(req.body);
        std::string element_name = request_data.value("element_name", "");
        bool active = request_data.value("active", true);

        if (element_name.empty()) {
            res.status = 400;
            res.set_content("{\"status\":\"error\",\"message\":\"element_name is required\"}",
                            "application/json");
            return;
        }

        // Get the pipeline
        GstElement *pipeline = GST_ELEMENT(gst_element_get_parent(GST_ELEMENT(self)));
        if (!pipeline) {
            res.status = 500;
            res.set_content("{\"status\":\"error\",\"message\":\"Could not get pipeline\"}",
                            "application/json");
            GST_ERROR_OBJECT(self, "Could not get pipeline");
            return;
        }

        // Find the element by name
        GstElement *target_element = gst_bin_get_by_name(GST_BIN(pipeline), element_name.c_str());
        gst_object_unref(pipeline);

        if (!target_element) {
            res.status = 404;
            json error_response = {{"status", "error"},
                                   {"message", "Element not found: " + element_name}};
            res.set_content(error_response.dump(), "application/json");
            GST_WARNING_OBJECT(self, "Element not found: %s", element_name.c_str());
            return;
        }

        // Set the active property
        g_object_set(target_element, "active", active, NULL);
        gst_object_unref(target_element);

        // Update the registry
        {
            std::lock_guard<std::mutex> lock(self->private_data->model_registry_mutex);
            for (auto &entry : self->private_data->model_registry) {
                if (entry.second.element_name == element_name) {
                    entry.second.active = active;
                    break;
                }
            }
        }

        GST_INFO_OBJECT(self, "Set element %s active=%d", element_name.c_str(), active);

        json response = {{"status", "ok"}, {"element_name", element_name}, {"active", active}};
        res.status = 200;
        res.set_content(response.dump(), "application/json");

    } catch (const json::exception &e) {
        res.status = 400;
        json error_response = {{"status", "error"},
                               {"message", std::string("Invalid JSON: ") + e.what()}};
        res.set_content(error_response.dump(), "application/json");
        GST_ERROR_OBJECT(self, "JSON parse error: %s", e.what());
    }
}

static gboolean toggle_on_main(gpointer user_data) {

    std::cout << "invoked\n";
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

    std::cout << "check is_pipeline\n";
    if (tsr->element && GST_IS_PIPELINE(tsr->element)) {
        std::cout << "is_pipeline\n";
        gst_object_unref(tsr->element);
        tsr->element = nullptr;
    }

    return G_SOURCE_REMOVE;
}

static void destroy_box(gpointer user_data) {
    delete static_cast<ToggleInvokeBox *>(user_data);
}

static void ctrl(GstAmpSink *self, const Request &req, Response &res) {

    // Add CORS headers
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");

    // Parse JSON request body
    std::string body = req.body;

    GST_INFO_OBJECT(self, "Received POST to /ctrl, body: %s", body.c_str());

    // Simple JSON parsing for {"enabled": true/false}
    bool enabled = (body.find("\"enabled\":true") != std::string::npos ||
                    body.find("\"enabled\": true") != std::string::npos);

    GST_INFO_OBJECT(self, "Control request: ampperformance enabled=%d", enabled);

    // Create custom upstream event for ampperformance
    GstStructure *structure =
        gst_structure_new("ampperformance", "enabled", G_TYPE_BOOLEAN, enabled, NULL);
    GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, structure);

    // Get the peer pad (source pad of upstream element connected to our sink)
    GstPad *sink_pad = gst_element_get_static_pad(GST_ELEMENT(self), "sink");
    if (sink_pad) {
        GstPad *peer_pad = gst_pad_get_peer(sink_pad);

        if (peer_pad) {
            GstElement *peer_elem = GST_ELEMENT(gst_pad_get_parent(peer_pad));
            GST_INFO_OBJECT(self,
                            "Sending event to peer element: %s",
                            peer_elem ? GST_ELEMENT_NAME(peer_elem) : "unknown");

            gboolean result = gst_pad_send_event(peer_pad, event);

            if (peer_elem)
                gst_object_unref(peer_elem);
            gst_object_unref(peer_pad);
            gst_object_unref(sink_pad);

            if (result) {
                res.status = 200;
                res.set_content("{\"status\":\"ok\"}", "application/json");
                GST_INFO_OBJECT(self, "Event sent successfully");
            } else {
                res.status = 500;
                res.set_content("{\"status\":\"error\",\"message\":\"Failed to send event\"}",
                                "application/json");
                GST_WARNING_OBJECT(self, "Failed to send event upstream");
            }
        } else {
            gst_object_unref(sink_pad);
            res.status = 500;
            res.set_content("{\"status\":\"error\",\"message\":\"No peer pad\"}",
                            "application/json");
            GST_ERROR_OBJECT(self, "Could not get peer pad");
        }
    } else {
        res.status = 500;
        res.set_content("{\"status\":\"error\",\"message\":\"No sink pad\"}", "application/json");
        GST_ERROR_OBJECT(self, "Could not get sink pad");
    }
}

static void play_pause(GstAmpSink *self, const Request &req, Response &res) {

    std::cout << "play-pause\n";
    auto tsr = std::make_shared<ToggleStateRequest>();

    GstElement *pipeline = get_top_pipeline(GST_ELEMENT(self));
    tsr->element = pipeline ? pipeline : GST_ELEMENT(self); // if pipeline, ref is held

    auto *box = new ToggleInvokeBox{tsr};
    g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, toggle_on_main, box, destroy_box);

    bool completed = false;
    {
        std::unique_lock<std::mutex> lk(tsr->m);
        completed = tsr->cv.wait_for(lk, std::chrono::milliseconds(800), [&] { return tsr->done; });
    }

    json out;
    if (!completed) {
        out["ok"] = false;
        out["error"] = "timeout waiting for state change";
        res.status = 504;
        res.set_content(out.dump(), "application/json");
        return;
    }

    // Read resulting under lock (or copy it while holding the lock)
    GstState resulting;
    {
        std::lock_guard<std::mutex> lk(tsr->m);
        resulting = tsr->resulting;
    }

    std::string state_str = "unknown";
    if (resulting == GST_STATE_PLAYING)
        state_str = "playing";
    else if (resulting == GST_STATE_PAUSED)
        state_str = "paused";

    out["ok"] = true;
    out["state"] = state_str;

    res.set_content(out.dump(), "application/json");
}
