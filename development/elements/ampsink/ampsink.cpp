/* Build:
g++ -fPIC -shared -o libgstampsink.so ampsink.cpp \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
*/

#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>
#include <iostream>
#include <mutex>

#define ASIO_STANDALONE
#include <asio.hpp>

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#include <amp/Tools.h>

#include <cpp-httplib/httplib.h>

#include <nlohmann/json.hpp>

#include <memory>
#include <thread>

#ifndef PACKAGE
#define PACKAGE "ampsink"
#endif

/* =============================== AmpSink ============================== */

#define STUN_SERVER "stun://stun.l.google.com:19302"

typedef struct _GstAmpSink GstAmpSink;
typedef struct _GstAmpSinkClass GstAmpSinkClass;

using ws_server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;
using json = nlohmann::json;

struct SessionContext {
    connection_hdl hdl;

    // aliases to make ws_server reachable from session negotiation functions
    std::shared_ptr<ws_server> ws;

    // Per-client GStreamer branch
    GstElement *webrtcbin = nullptr;
    GstElement *queue = nullptr; // between tee and webrtcbin

    GstPad *tee_src_pad = nullptr;     // requested from tee
    GstPad *webrtc_sink_pad = nullptr; // requested from webrtcbin ("sink_%u")
};

using WebRtcSessions =
    std::map<connection_hdl, std::shared_ptr<SessionContext>, std::owner_less<connection_hdl>>;

struct ModelStatus {
    std::string name;
    bool active;
    std::string element_name;
};

struct GstAmpPrivate {
    std::thread http_server_thread;
    std::thread ws_server_thread;
    std::unique_ptr<httplib::Server> http_server;

    std::shared_ptr<ws_server> ws;

    std::mutex webrtc_session_mutex;
    WebRtcSessions webrtc_sessions;

    std::mutex model_registry_mutex;
    std::map<std::string, ModelStatus> model_registry; // key: element_name
};

struct _GstAmpSink {
    GstBin parent;

    GstElement *vconv;
    GstElement *queue;
    GstElement *vp8enc;
    GstElement *rtpvp8pay;
    GstElement *tee;

    // Drain branch to make the pipeline complete
    GstElement *drain_queue;
    GstElement *drain_fakesink;
    GstPad *drain_tee_src_pad;

    /* properties */
    gchar *host;
    gchar *static_files_location;
    gint http_port;
    gint ws_port;

    GstAmpPrivate *private_data;
};

struct _GstAmpSinkClass {
    GstBinClass parent_class;
};

GType gst_amp_sink_get_type(void);
#define GST_TYPE_AMP_SINK (gst_amp_sink_get_type())
G_DEFINE_TYPE(GstAmpSink, gst_amp_sink, GST_TYPE_BIN)

/* ===== Utils ===== */

static gboolean have_element(const char *name) {
    GstElementFactory *f = gst_element_factory_find(name);
    if (f) {
        gst_object_unref(f);
        return TRUE;
    }
    return FALSE;
}

static void set_int_if_prop_exists(GstElement *e, const char *prop, gint value) {
    if (!e)
        return;
    GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
    if (ps)
        g_object_set(e, prop, value, NULL);
}

static void push_props_down(GstAmpSink *self) {
    if (self->vp8enc) {
    }
}

/* ===== Properties ===== */
enum {
    PROP_0,
    PROP_HOST,
    PROP_HTTP_PORT,
    PROP_WS_PORT,
    PROP_STATIC_FILES,
};

static void
gst_amp_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);
    switch (prop_id) {
    case PROP_HOST:
        g_free(self->host);
        self->host = g_value_dup_string(value);
        break;
    case PROP_HTTP_PORT:
        self->http_port = g_value_get_int(value);
        break;
    case PROP_WS_PORT:
        self->ws_port = g_value_get_int(value);
        break;
    case PROP_STATIC_FILES:
        g_free(self->static_files_location);
        self->static_files_location = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        return;
    }
    push_props_down(self);
}

static void
gst_amp_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);
    switch (prop_id) {
    case PROP_HOST:
        g_value_set_string(value, self->host);
        break;
    case PROP_HTTP_PORT:
        g_value_set_int(value, self->http_port);
        break;
    case PROP_WS_PORT:
        g_value_set_int(value, self->ws_port);
        break;
    case PROP_STATIC_FILES:
        g_value_set_string(value, self->static_files_location);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

/* ===== Pad templates ===== */
static GstStaticPadTemplate v_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate a_sink_template = GST_STATIC_PAD_TEMPLATE(
    "audiopad", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS("audio/x-raw"));

/* ===== Request/Release pads (audio, MP2 path + audiorate) ===== */

static GstPad *gst_amp_sink_request_new_pad(GstElement *element,
                                            GstPadTemplate *templ,
                                            const gchar *name,
                                            const GstCaps *caps) {
    auto *self = reinterpret_cast<GstAmpSink *>(element);
    const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE(templ);
    if (g_strcmp0(templ_name, "audiopad") != 0)
        return nullptr;

    /* Apply bitrate now that encoder exists */
    push_props_down(self);

    return nullptr;
}

static void gst_amp_sink_release_pad(GstElement *element, GstPad *pad) {
    gst_element_remove_pad(element, pad);
}

/* ===== Event handling ===== */
static gboolean gst_amp_sink_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    auto *self = reinterpret_cast<GstAmpSink *>(parent);

    if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
        const GstStructure *structure = gst_event_get_structure(event);

        if (gst_structure_has_name(structure, "amp-model-register")) {
            const gchar *model_name = gst_structure_get_string(structure, "model-name");
            const gchar *element_name = gst_structure_get_string(structure, "element-name");
            gboolean active = FALSE;
            gst_structure_get_boolean(structure, "active", &active);

            if (model_name && element_name) {
                std::lock_guard<std::mutex> lock(self->private_data->model_registry_mutex);
                ModelStatus status;
                status.name = model_name;
                status.active = active;
                status.element_name = element_name;
                self->private_data->model_registry[element_name] = status;

                std::cout << "[ampsink] Registered model: " << model_name
                          << " from element: " << element_name
                          << " (active: " << (active ? "yes" : "no") << ")" << std::endl;
            }

            // Consume the event (don't pass it further)
            gst_event_unref(event);
            return TRUE;
        } else if (gst_structure_has_name(structure, "amp-model-unregister")) {
            const gchar *element_name = gst_structure_get_string(structure, "element-name");

            if (element_name) {
                std::lock_guard<std::mutex> lock(self->private_data->model_registry_mutex);
                auto it = self->private_data->model_registry.find(element_name);
                if (it != self->private_data->model_registry.end()) {
                    std::cout << "[ampsink] Unregistered model: " << it->second.name
                              << " from element: " << element_name << std::endl;
                    self->private_data->model_registry.erase(it);
                }
            }

            // Consume the event (don't pass it further)
            gst_event_unref(event);
            return TRUE;
        }
    }

    // Pass all other events (CAPS, SEGMENT, EOS, etc.) to the target pad
    GstPad *target = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    if (target) {
        gboolean ret = gst_pad_send_event(target, event);
        gst_object_unref(target);
        return ret;
    }

    gst_event_unref(event);
    return FALSE;
}

/* ===== Lifecycle ===== */
static void gst_amp_sink_dispose(GObject *object) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);

    if (self->private_data->http_server) {
        self->private_data->http_server->stop();
    }

    if (self->private_data->http_server_thread.joinable()) {
        self->private_data->http_server_thread.join();
    }

    if (self->private_data->ws) {
        self->private_data->ws->stop();
    }

    if (self->private_data->ws_server_thread.joinable()) {
        self->private_data->ws_server_thread.join();
    }

    g_clear_pointer(&self->host, g_free);
    g_clear_pointer(&self->static_files_location, g_free);

    delete self->private_data;

    G_OBJECT_CLASS(gst_amp_sink_parent_class)->dispose(object);
}

static void gst_amp_sink_setup_http_server(GstAmpSink *self) {
    using namespace httplib;

    auto &http_server = self->private_data->http_server;

    self->private_data->http_server = std::make_unique<Server>();

    // Dynamic config endpoint
    http_server->Get("/amp-config.js", [self](const Request &req, Response &res) {
        std::string js = "window.AMP_CONFIG = { wsPort: " + std::to_string(self->ws_port) + " };";
        res.set_content(js, "application/javascript");
    });

    // Model registry endpoint
    http_server->Get("/models", [self](const Request &req, Response &res) {
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
    });

    // Model toggle endpoint
    http_server->Post("/models/toggle", [self](const Request &req, Response &res) {
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
            GstElement *target_element =
                gst_bin_get_by_name(GST_BIN(pipeline), element_name.c_str());
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
    });

    auto ret = http_server->set_mount_point("/", self->static_files_location);
    if (!ret) {
        // TODO@ibori: error handling
        throw std::runtime_error(
            std::string("Static file directory does not exist: ") +
            (self->static_files_location ? self->static_files_location : "(null)"));
    }

    http_server->Post("/ctrl", [self](const Request &req, Response &res) {
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
            res.set_content("{\"status\":\"error\",\"message\":\"No sink pad\"}",
                            "application/json");
            GST_ERROR_OBJECT(self, "Could not get sink pad");
        }
    });

    // TODO@ibori: error handling
    auto started = http_server->listen(self->host, self->http_port);
    if (!started) {
        // TODO@ibori: error handling
        throw std::runtime_error(std::string("HTTP server failed to start on ") +
                                 std::string(self->host ? self->host : "<null>") + ":" +
                                 std::to_string(self->http_port));
    }
}

static void send_text(SessionContext *ctx, const std::string &text) {
    try {
        ctx->ws->send(ctx->hdl, text, websocketpp::frame::opcode::text);
    } catch (const websocketpp::exception &e) {
        std::cerr << "WebSocket send error: " << e.what() << std::endl;
    }
}

void send_ice_candidate_message(SessionContext *ctx, guint mlineindex, gchar *candidate) {
    std::cout << "Sending ICE candidate: mlineindex=" << mlineindex << ", candidate=" << candidate
              << std::endl;
    json msg;
    msg["type"] = "candidate";
    msg["ice"] = {{"candidate", candidate}, {"sdpMLineIndex", mlineindex}};

    send_text(ctx, msg.dump());

    std::cout << "ICE candidate sent" << std::endl;
}

void on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data) {
    std::cout << "ICE candidate generated: mlineindex=" << mlineindex << ", candidate=" << candidate
              << std::endl;

    SessionContext *ctx = static_cast<SessionContext *>(user_data);
    send_ice_candidate_message(ctx, mlineindex, candidate);
}

void on_negotiation_needed(GstElement *webrtc, gpointer user_data) {
    std::cout << "Negotiation needed" << std::endl;
}

void on_open(GstAmpSink *self, std::shared_ptr<ws_server> ws, connection_hdl hdl) {
    std::cout << "WebSocket connection opened" << std::endl;

    auto ctx = std::make_shared<SessionContext>();
    ctx->ws = ws;
    ctx->hdl = hdl;

    // --- Create per-client elements ---
    ctx->queue = gst_element_factory_make("queue", nullptr);
    ctx->webrtcbin = gst_element_factory_make("webrtcbin", nullptr);

    if (!ctx->queue || !ctx->webrtcbin) {
        g_printerr("Failed to create per-client queue or webrtcbin\n");
        if (ctx->queue)
            gst_object_unref(ctx->queue);
        if (ctx->webrtcbin)
            gst_object_unref(ctx->webrtcbin);
        return;
    }

    g_object_set(ctx->webrtcbin, "stun-server", STUN_SERVER, nullptr);

    // Add to ampsink bin
    gst_bin_add_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);

    // === 1) tee → client queue ===
    ctx->tee_src_pad = gst_element_request_pad_simple(self->tee, "src_%u");
    if (!ctx->tee_src_pad) {
        g_printerr("Failed to request src pad from tee for client branch\n");
        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    GstPad *queue_sink_pad = gst_element_get_static_pad(ctx->queue, "sink");
    if (!queue_sink_pad) {
        g_printerr("Failed to get sink pad of client queue\n");
        gst_element_release_request_pad(self->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    if (gst_pad_link(ctx->tee_src_pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link tee -> client queue\n");
        gst_object_unref(queue_sink_pad);
        gst_element_release_request_pad(self->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }
    gst_object_unref(queue_sink_pad);

    // === 2) client queue → webrtcbin sink_%u ===
    GstPad *queue_src_pad = gst_element_get_static_pad(ctx->queue, "src");
    if (!queue_src_pad) {
        g_printerr("Failed to get src pad of client queue\n");

        // undo tee → queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    ctx->webrtc_sink_pad = gst_element_request_pad_simple(ctx->webrtcbin, "sink_%u");
    if (!ctx->webrtc_sink_pad) {
        g_printerr("Failed to request sink pad on webrtcbin\n");
        gst_object_unref(queue_src_pad);

        // undo tee → queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    if (gst_pad_link(queue_src_pad, ctx->webrtc_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link client queue src -> webrtcbin sink\n");
        gst_object_unref(queue_src_pad);

        gst_element_release_request_pad(ctx->webrtcbin, ctx->webrtc_sink_pad);
        gst_object_unref(ctx->webrtc_sink_pad);
        ctx->webrtc_sink_pad = nullptr;

        // undo tee → queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    gst_object_unref(queue_src_pad);

    // Make sure new elements follow the bin's current state
    gst_element_sync_state_with_parent(ctx->queue);
    gst_element_sync_state_with_parent(ctx->webrtcbin);

    // Hook WebRTC callbacks to this session
    g_signal_connect(
        ctx->webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), ctx.get());
    g_signal_connect(ctx->webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), ctx.get());

    std::lock_guard<std::mutex> mutex_guard(self->private_data->webrtc_session_mutex);
    self->private_data->webrtc_sessions[hdl] = ctx;

    std::cout << "Per-client WebRTC branch created and attached to tee\n";
}

void on_close(GstAmpSink *self, connection_hdl hdl) {
    std::cout << "WebSocket connection closed" << std::endl;

    std::lock_guard<std::mutex> mutex_guard(self->private_data->webrtc_session_mutex);
    auto &sessions = self->private_data->webrtc_sessions;

    auto it = sessions.find(hdl);
    if (it == sessions.end()) {
        return;
    }

    auto ctx = it->second;

    // 1) Stop per-client elements
    if (ctx->webrtcbin)
        gst_element_set_state(ctx->webrtcbin, GST_STATE_NULL);
    if (ctx->queue)
        gst_element_set_state(ctx->queue, GST_STATE_NULL);

    // 2) Unlink tee -> client queue and release the tee src pad
    if (ctx->tee_src_pad) {
        GstPad *queue_sink = nullptr;
        if (ctx->queue) {
            queue_sink = gst_element_get_static_pad(ctx->queue, "sink");
        }

        if (queue_sink) {
            gst_pad_unlink(ctx->tee_src_pad, queue_sink);
            gst_object_unref(queue_sink);
        }

        gst_element_release_request_pad(self->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
    }

    // 3) Release webrtcbin sink_%u pad
    if (ctx->webrtc_sink_pad && ctx->webrtcbin) {
        gst_element_release_request_pad(ctx->webrtcbin, ctx->webrtc_sink_pad);
        gst_object_unref(ctx->webrtc_sink_pad);
        ctx->webrtc_sink_pad = nullptr;
    }

    // 4) Remove per-client elements from the bin
    if (ctx->queue || ctx->webrtcbin) {
        gst_bin_remove_many(GST_BIN(self), ctx->queue, ctx->webrtcbin, nullptr);
    }

    ctx->queue = nullptr;
    ctx->webrtcbin = nullptr;

    // 5) Forget the session
    sessions.erase(it);
}

void on_answer_created(GstPromise *promise, gpointer user_data) {
    std::cout << "Answer created" << std::endl;

    SessionContext *ctx = static_cast<SessionContext *>(user_data);

    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);

    GstPromise *local_promise = gst_promise_new();
    g_signal_emit_by_name(ctx->webrtcbin, "set-local-description", answer, local_promise);

    json sdp_json;
    sdp_json["type"] = "answer";
    sdp_json["sdp"] = gst_sdp_message_as_text(answer->sdp);
    send_text(ctx, sdp_json.dump());

    std::cout << "Local description set and answer sent: " << sdp_json.dump() << std::endl;

    gst_webrtc_session_description_free(answer);
}

void on_set_remote_description(GstPromise *promise, gpointer user_data) {
    std::cout << "Remote description set, creating answer" << std::endl;

    SessionContext *ctx = static_cast<SessionContext *>(user_data);
    GstPromise *answer_promise = gst_promise_new_with_change_func(on_answer_created, ctx, NULL);

    g_signal_emit_by_name(ctx->webrtcbin, "create-answer", NULL, answer_promise);
}

void on_message(GstAmpSink *self,
                std::shared_ptr<ws_server> server,
                connection_hdl hdl,
                ws_server::message_ptr msg) {
    std::lock_guard<std::mutex> mutex_guard(self->private_data->webrtc_session_mutex);
    auto &webrtc_sessions = self->private_data->webrtc_sessions;

    try {
        auto it = webrtc_sessions.find(hdl);
        if (it == webrtc_sessions.end()) {
            std::cerr << "No session context for this connection" << std::endl;
            return;
        }
        auto ctx = it->second;

        const std::string payload = msg->get_payload();
        json j = json::parse(payload);

        std::string type = j["type"].get<std::string>();

        if (type == "offer") {
            std::cout << "Received offer: " << payload << std::endl;

            std::string sdp = j["sdp"].get<std::string>();
            GstSDPMessage *sdp_message = nullptr;
            if (gst_sdp_message_new_from_text(sdp.c_str(), &sdp_message) != GST_SDP_OK) {
                g_printerr("Failed to parse SDP offer\n");
                return;
            }

            GstWebRTCSessionDescription *offer =
                gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_message);

            GstPromise *promise =
                gst_promise_new_with_change_func(on_set_remote_description, ctx.get(), NULL);
            g_signal_emit_by_name(ctx->webrtcbin, "set-remote-description", offer, promise);
            gst_webrtc_session_description_free(offer);

            std::cout << "Setting remote description" << std::endl;
        } else if (type == "candidate") {
            std::cout << "Received ICE candidate: " << payload << std::endl;

            auto ice = j["ice"];
            std::string candidate = ice["candidate"].get<std::string>();
            guint sdpMLineIndex = static_cast<guint>(ice["sdpMLineIndex"].get<int>());

            g_signal_emit_by_name(
                ctx->webrtcbin, "add-ice-candidate", sdpMLineIndex, candidate.c_str());

            std::cout << "Added ICE candidate" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "on_message exception: " << e.what() << std::endl;
    }
}

static void gst_amp_sink_setup_ws_server(GstAmpSink *self) {
    std::cout << "started ws server\n";

    auto server = std::make_shared<ws_server>();

    server->init_asio();

    self->private_data->ws = server;

    server->set_open_handler(
        [self](connection_hdl hdl) { on_open(self, self->private_data->ws, hdl); });

    server->set_close_handler([self](connection_hdl hdl) { on_close(self, hdl); });

    server->set_message_handler([self](connection_hdl hdl, ws_server::message_ptr msg) {
        on_message(self, self->private_data->ws, hdl, msg);
    });

    server->set_reuse_addr(true);
    server->listen(self->ws_port);
    server->start_accept();

    std::cout << "WebSocket++ server listening on port " << self->ws_port << std::endl;
    server->run();
}

static void gst_amp_sink_init(GstAmpSink *self) {
    self->private_data = new GstAmpPrivate();

    /* defaults */
    self->host = g_strdup(amp::Tools::getLocalIp().c_str());
    self->static_files_location = g_strdup("./scripts/public");
    self->http_port = 9999;
    self->ws_port = 8000;

    self->vconv = gst_element_factory_make("videoconvert", "vconv");
    self->queue = gst_element_factory_make("queue", "vqueue");
    self->vp8enc = gst_element_factory_make("vp8enc", "vp8enc");
    self->rtpvp8pay = gst_element_factory_make("rtpvp8pay", "rtpvp8pay");
    self->tee = gst_element_factory_make("tee", "rtp_tee");

    g_return_if_fail(self->vconv && self->queue && self->vp8enc && self->rtpvp8pay && self->tee);

    g_object_set(self->vp8enc, "deadline", 1, NULL);

    gst_bin_add_many(
        GST_BIN(self), self->vconv, self->queue, self->vp8enc, self->rtpvp8pay, self->tee, NULL);

    if (!gst_element_link_many(
            self->vconv, self->queue, self->vp8enc, self->rtpvp8pay, self->tee, NULL)) {
        GST_ERROR_OBJECT(self, "Failed to link video chain");
    }

    /* --- DRAIN BRANCH: tee → drain_queue → fakesink --- */
    self->drain_queue = gst_element_factory_make("queue", "drain_queue");
    self->drain_fakesink = gst_element_factory_make("fakesink", "drain_fakesink");

    g_return_if_fail(self->drain_queue && self->drain_fakesink);

    // fakesink should not block or sync to clock
    g_object_set(self->drain_fakesink, "sync", FALSE, "async", FALSE, NULL);

    gst_bin_add_many(GST_BIN(self), self->drain_queue, self->drain_fakesink, NULL);

    if (!gst_element_link(self->drain_queue, self->drain_fakesink)) {
        GST_ERROR_OBJECT(self, "Failed to link drain_queue -> drain_fakesink");
    }

    // Connect tee → drain_queue
    self->drain_tee_src_pad = gst_element_request_pad_simple(self->tee, "src_%u");
    if (!self->drain_tee_src_pad) {
        GST_ERROR_OBJECT(self, "Failed to request src pad from tee for drain");
    } else {
        GstPad *drain_sink = gst_element_get_static_pad(self->drain_queue, "sink");
        if (!drain_sink) {
            GST_ERROR_OBJECT(self, "Failed to get sink pad of drain_queue");
        } else {
            if (gst_pad_link(self->drain_tee_src_pad, drain_sink) != GST_PAD_LINK_OK) {
                GST_ERROR_OBJECT(self, "Failed to link tee -> drain_queue");
            }
            gst_object_unref(drain_sink);
        }
    }

    // Make sure drain elements follow the bin state
    gst_element_sync_state_with_parent(self->drain_queue);
    gst_element_sync_state_with_parent(self->drain_fakesink);

    /* expose ALWAYS video ghost pad */
    {
        GstPad *vs = gst_element_get_static_pad(self->vconv, "sink");
        GstPad *vg = gst_ghost_pad_new("sink", vs);
        gst_object_unref(vs);

        // Install custom event handler
        gst_pad_set_event_function(vg, gst_amp_sink_sink_event);

        gst_element_add_pad(GST_ELEMENT(self), vg);
    }

    push_props_down(self);

    self->private_data->ws_server_thread = std::thread(gst_amp_sink_setup_ws_server, self);
    self->private_data->http_server_thread = std::thread(gst_amp_sink_setup_http_server, self);
}

static void gst_amp_sink_class_init(GstAmpSinkClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_amp_sink_set_property;
    gobject_class->get_property = gst_amp_sink_get_property;
    gobject_class->dispose = gst_amp_sink_dispose;

    /* C++ flags helper */
    constexpr GParamFlags kRW =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* properties */
    g_object_class_install_property(
        gobject_class,
        PROP_HOST,
        g_param_spec_string(
            "host", "Host", "The interface to bind the HTTP Server", "0.0.0.0", kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_HTTP_PORT,
        g_param_spec_int("http-port", "HTTP Port", "HTTP Server Port", 1, 65535, 9999, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_WS_PORT,
        g_param_spec_int("ws-port", "WebSocket Port", "WebSocket Port", 1, 65535, 8000, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_STATIC_FILES,
        g_param_spec_string("static-files",
                            "Static Files Location",
                            "Location of the static files for HTTP Server",
                            "./scripts/public",
                            kRW));

    /* pads */
    gst_element_class_add_static_pad_template(element_class, &v_sink_template);
    gst_element_class_add_static_pad_template(element_class, &a_sink_template);

    /* request/release handlers for audio */
    element_class->request_new_pad = gst_amp_sink_request_new_pad;
    element_class->release_pad = gst_amp_sink_release_pad;

    gst_element_class_set_static_metadata(
        element_class,
        "AmpSink (video+audio → raw video+audio -> VP8 -> WebRTC)",
        "Sink/Network/Bin",
        "Encodes & muxes raw video+audio and sends them to WebRTC",
        "Your Name <you@example.com>");
}

/* ===== Plugin boilerplate ===== */
static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "ampsink", GST_RANK_NONE, GST_TYPE_AMP_SINK);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ampsink,
                  "AmpSink bin: raw video+audio -> VP8 -> WebRTC ",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
