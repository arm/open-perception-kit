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

#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <glib.h>
#include <glibconfig.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

#include "file_writer.h"
#include "tcp_writer.h"
#include "websocket_writer.h"
#include "writer.h"

#include <gst/FrameResultsMeta.h>

#ifdef OPK_FRAME_ACCESS
#include "frame_access_server.h"
#include "mediaio/GstVideoFrame.h"
#include <dlfcn.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#endif

#ifndef PACKAGE
constexpr const char *PACKAGE = "opkcomm";
#endif

/* ----------------------- Element definition ----------------------- */

#define GST_TYPE_OPK_COMM (gst_opk_comm_get_type())
G_DECLARE_FINAL_TYPE(GstOpkComm, gst_opk_comm, GST, OPK_COMM, GstBaseTransform)

typedef enum {

    GST_OPK_COMM_METHOD_FILE = 1,
    GST_OPK_COMM_METHOD_WEBSOCKET = 2,
    GST_OPK_COMM_METHOD_TCP = 3,
#ifdef OPK_FRAME_ACCESS
    GST_OPK_COMM_METHOD_ICEORYX2 = 4,
#endif
} GstOpkCommMethod;

struct GstOpkCommPrivate {
    std::recursive_mutex writer_mutex;
    // Property readback must remain available while writer shutdown joins logging threads.
    std::mutex property_mutex;
    std::unique_ptr<Writer> writer;
    bool started = false;
    bool writer_open = false;
#ifdef OPK_FRAME_ACCESS
    std::string frame_endpoint;
    std::string frame_profile;
    std::string runtime_path;
    std::optional<opk::frame_access::DeploymentProfile> profile;
    std::unique_ptr<opk::frame_access::Server> frame_server;
    std::atomic_bool frame_ready{false};
    GstVideoInfo video_info{};
#endif
};

struct _GstOpkComm {
    GstBaseTransform parent;

    GstOpkCommMethod method;
    gchar *file_name;
    guint ws_port;
    gchar *endpoint;
    gchar *tcp_host;
    guint tcp_port;

    uint64_t frame_counter;

    GstOpkCommPrivate *priv;
};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE(GstOpkComm, gst_opk_comm, GST_TYPE_BASE_TRANSFORM)

/* ----------------------- Properties ----------------------- */

namespace {

enum class PropertyId : guint {
    Reserved = 0,
    Method,
    FileName,
    WsPort,
    Endpoint,
    TcpHost,
    TcpPort,
#ifdef OPK_FRAME_ACCESS
    FrameEndpoint,
    FrameProfile,
    RuntimePath,
    FrameReady,
#endif
};

} // namespace

static GType gst_opk_comm_method_get_type(void) {
    static GType methodType = 0;
    static std::vector<GEnumValue> values = {
        {GST_OPK_COMM_METHOD_FILE, "file", "file"},
        {GST_OPK_COMM_METHOD_WEBSOCKET, "websocket", "websocket"},
        {GST_OPK_COMM_METHOD_TCP, "tcp", "tcp"},
#ifdef OPK_FRAME_ACCESS
        {GST_OPK_COMM_METHOD_ICEORYX2, "iceoryx2", "iceoryx2"},
#endif
        {0, nullptr, nullptr}};

    if (g_once_init_enter(&methodType)) {
        GType registeredType = g_enum_register_static("GstOpkCommMethod", values.data());
        g_once_init_leave(&methodType, registeredType);
    }
    return methodType;
}

/* ----------------------- Helpers ----------------------- */

static const char *gst_opk_comm_method_name(GstOpkCommMethod method) {
    switch (method) {
    case GST_OPK_COMM_METHOD_FILE:
        return "file";
    case GST_OPK_COMM_METHOD_WEBSOCKET:
        return "websocket";
    case GST_OPK_COMM_METHOD_TCP:
        return "tcp";
#ifdef OPK_FRAME_ACCESS
    case GST_OPK_COMM_METHOD_ICEORYX2:
        return "iceoryx2";
#endif
    default:
        return "unknown";
    }
}

static gchar *gst_opk_comm_open_failure_details(const GstOpkComm *self) {
    return g_strdup_printf("method=%s file-name='%s' ws-port=%u endpoint='%s' "
                           "tcp-host='%s' tcp-port=%u",
                           gst_opk_comm_method_name(self->method),
                           self->file_name ? self->file_name : "",
                           self->ws_port,
                           self->endpoint ? self->endpoint : "",
                           self->tcp_host ? self->tcp_host : "",
                           self->tcp_port);
}

static bool gst_opk_comm_open_io(GstOpkComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }
    self->priv->writer_open = false;

    auto success = false;
    switch (self->method) {
#ifdef OPK_FRAME_ACCESS
    case GST_OPK_COMM_METHOD_ICEORYX2:
        // Validate explicit limits now; endpoint creation waits for negotiated caps.
        self->priv->writer.reset();
        try {
            if (self->priv->frame_endpoint.empty()) {
                throw std::invalid_argument("frame-endpoint must be explicitly configured");
            }

            const auto json = nlohmann::json::parse(self->priv->frame_profile);
            opk::frame_access::DeploymentProfile profile{};
            const std::array fields{
                std::pair{"clients", &profile.clients},
                std::pair{"active_requests_per_client", &profile.activeRequestsPerClient},
                std::pair{"callback_attempts", &profile.callbackAttempts},
                std::pair{"max_image_bytes", &profile.maxImageBytes},
                std::pair{"max_results_bytes", &profile.maxResultsBytes},
                std::pair{"max_construction_bytes", &profile.maxConstructionBytes}};
            if (!json.is_object() || json.size() != fields.size()) {
                throw std::invalid_argument("frame-profile requires exactly six explicit limits");
            }

            for (const auto &[name, target] : fields) {
                if (!json.contains(name) || !json[name].is_number_unsigned() ||
                    json[name].get<uint64_t>() == 0) {
                    throw std::invalid_argument(std::string("invalid frame-profile limit: ") +
                                                name);
                }
                *target = json[name].get<uint64_t>();
            }

            self->priv->profile = profile;
            success = true;
        } catch (const std::exception &error) {
            GST_ERROR_OBJECT(self, "Invalid frame-access configuration: %s", error.what());
        }
        break;
#endif
    case GST_OPK_COMM_METHOD_FILE:
        self->priv->writer = std::make_unique<FileWriter>(self, self->file_name, 5);
        success = self->priv->writer->start();
        break;
    case GST_OPK_COMM_METHOD_WEBSOCKET:
        self->priv->writer = std::make_unique<WebSocketWriter>(
            self, static_cast<uint16_t>(self->ws_port), self->endpoint ? self->endpoint : "/ws", 5);
        success = self->priv->writer->start();
        break;
    case GST_OPK_COMM_METHOD_TCP:
        self->priv->writer = std::make_unique<TcpWriter>(
            self, self->tcp_host ? self->tcp_host : "", static_cast<uint16_t>(self->tcp_port), 5);
        success = self->priv->writer->start();
        break;
    default:
        GST_WARNING_OBJECT(
            self, "Unknown opkcomm publishing method: %d", static_cast<int>(self->method));
        break;
    }

    if (success) {
        self->priv->writer_open = true;
    } else {
        self->priv->writer.reset();
    }

    return success;
}

static void gst_opk_comm_close_io(GstOpkComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }
    self->priv->writer.reset();
    self->priv->writer_open = false;
#ifdef OPK_FRAME_ACCESS
    self->priv->frame_ready.store(false);
    self->priv->frame_server.reset();
    self->priv->profile.reset();
    gst_video_info_init(&self->priv->video_info);
#endif
}

/* ----------------------- GstBaseTransform vfuncs ----------------------- */

#ifdef OPK_FRAME_ACCESS
static std::string frame_schema_path() {
    // Resolve beside the loaded plugin so relocatable packages do not use a build-host path.
    static constexpr char anchor = '\0';
    Dl_info library{};
    if (dladdr(&anchor, &library) != 0 && library.dli_fname != nullptr) {
        const auto directory = std::filesystem::absolute(library.dli_fname).parent_path();
        for (const auto *relative : {"../../share/opk/frame-access/frame_reply.bfbs",
                                     "../../../share/opk/frame-access/frame_reply.bfbs"}) {
            const auto candidate = (directory / relative).lexically_normal();
            if (std::filesystem::is_regular_file(candidate))
                return candidate.string();
        }
    }

    if (std::filesystem::is_regular_file(OPK_FRAME_SCHEMA_PATH))
        return OPK_FRAME_SCHEMA_PATH;
    throw std::runtime_error("frame_reply.bfbs is missing from the OPK package");
}

static gboolean gst_opk_comm_set_caps(GstBaseTransform *parent, GstCaps *input, GstCaps *) {
    auto *self = GST_OPK_COMM(parent);
    const std::scoped_lock lock(self->priv->writer_mutex);
    if (self->method != GST_OPK_COMM_METHOD_ICEORYX2)
        return TRUE;

    self->priv->frame_ready.store(false);
    GstVideoInfo info{};
    if (!gst_video_info_from_caps(&info, input))
        return FALSE;

    namespace wire = opk::frame_access::v1;
    auto format = wire::PixelFormat_Unknown;
    switch (GST_VIDEO_INFO_FORMAT(&info)) {
    case GST_VIDEO_FORMAT_BGRA:
        format = wire::PixelFormat_BGRA8;
        break;
    case GST_VIDEO_FORMAT_RGB:
        format = wire::PixelFormat_RGB8;
        break;
    case GST_VIDEO_FORMAT_GRAY8:
        format = wire::PixelFormat_Gray8;
        break;
    case GST_VIDEO_FORMAT_I420:
        format = wire::PixelFormat_I420;
        break;
    case GST_VIDEO_FORMAT_NV12:
        format = wire::PixelFormat_NV12;
        break;
    case GST_VIDEO_FORMAT_YUY2:
        format = wire::PixelFormat_YUY2;
        break;
    default:
        return FALSE;
    }

    // Caps determine the initial byte reservation; later layouts must fit it without resizing.
    try {
        if (!self->priv->profile)
            throw std::runtime_error("frame-access profile was not initialized");
        if (self->priv->frame_server) {
            if (!self->priv->frame_server->setLayout(format, info.width, info.height)) {
                throw std::runtime_error("renegotiated frame exceeds endpoint construction bounds");
            }
        } else {
            self->priv->frame_server = std::make_unique<opk::frame_access::Server>(
                *self->priv->profile,
                self->priv->frame_endpoint,
                self->priv->runtime_path,
                frame_schema_path(),
                format,
                info.width,
                info.height,
                [self](const std::string_view error) {
                    GST_WARNING_OBJECT(self, "%.*s", static_cast<int>(error.size()), error.data());
                });
        }

        self->priv->video_info = info;
        self->priv->frame_ready.store(true);
        return TRUE;
    } catch (const std::exception &error) {
        GST_ERROR_OBJECT(self, "Frame-access setup failed: %s", error.what());
        return FALSE;
    }
}
#endif

static gboolean gst_opk_comm_start(GstBaseTransform *selfInParentType) {
    GstOpkComm *self = GST_OPK_COMM(selfInParentType);
    std::unique_lock lock(self->priv->writer_mutex);

    self->priv->started = true;
    if (!gst_opk_comm_open_io(self)) {
        self->priv->started = false;
        g_autofree gchar *failureDetails = gst_opk_comm_open_failure_details(self);
        lock.unlock();
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          OPEN_WRITE,
                          ("Failed to open opkcomm metadata writer"),
                          ("%s", failureDetails));
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_opk_comm_stop(GstBaseTransform *selfInParentType) {
    GstOpkComm *self = GST_OPK_COMM(selfInParentType);
    const std::scoped_lock lock(self->priv->writer_mutex);
    self->priv->started = false;
    gst_opk_comm_close_io(self);

    return TRUE;
}

static GstFlowReturn gst_opk_comm_transform_ip(GstBaseTransform *selfInParentType,
                                               GstBuffer *buffer) {
    GstOpkComm *self = GST_OPK_COMM(selfInParentType);
    std::unique_lock lock(self->priv->writer_mutex);

    if (!self->priv->writer_open)
        return GST_FLOW_ERROR;

#ifdef OPK_FRAME_ACCESS
    if (self->method == GST_OPK_COMM_METHOD_ICEORYX2) {
        if (!self->priv->frame_server || self->frame_counter == UINT64_MAX) {
            lock.unlock();
            GST_ELEMENT_ERROR(self,
                              RESOURCE,
                              FAILED,
                              ("Frame-access endpoint unavailable or sequence exhausted"),
                              (nullptr));
            return GST_FLOW_ERROR;
        }

        // Use this completed buffer for both results and pixels, without an extra-frame wait.
        const auto results = opk::FrameResultsMeta::read(buffer);
        const auto pts = GST_BUFFER_PTS_IS_VALID(buffer)
                             ? std::optional<uint64_t>(GST_BUFFER_PTS(buffer))
                             : std::nullopt;

        // Mapping is deferred: metadata-only requests must not touch source pixels.
        self->priv->frame_server->serve(results.get(), self->frame_counter, pts, [self, buffer]() {
            if (GST_VIDEO_INFO_INTERLACE_MODE(&self->priv->video_info) !=
                    GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ||
                GST_BUFFER_FLAG_IS_SET(buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED) ||
                GST_BUFFER_FLAG_IS_SET(buffer, GST_VIDEO_BUFFER_FLAG_ONEFIELD)) {
                return std::shared_ptr<opk::mediaio::VideoFrame>{};
            }
            return std::static_pointer_cast<opk::mediaio::VideoFrame>(
                opk::mediaio::gst::GstVideoFrame::mapGstBuffer(buffer, self->priv->video_info));
        });

        // Identity advances per buffer, not per request; clients may share a sequence or see gaps.
        ++self->frame_counter;
        return GST_FLOW_OK;
    }
#endif

    if (self->priv->writer && self->priv->writer->check_running()) {
        auto frameResults = opk::FrameResultsMeta::read(buffer);
        OpkCommJob job = {
            .frame_counter = self->frame_counter,
            .frameResults = frameResults,
        };

        self->priv->writer->send(std::move(job));
    }

    ++self->frame_counter;

    /* passthrough: do not modify buffer */
    return GST_FLOW_OK;
}

/* ----------------------- GObject plumbing ----------------------- */

static void gst_opk_comm_set_property(GObject *object,
                                      guint propertyId,
                                      const GValue *value,
                                      GParamSpec *propertySpec) {
    GstOpkComm *self = GST_OPK_COMM(object);
    std::unique_lock lock(self->priv->writer_mutex);
    std::unique_lock propertyLock(self->priv->property_mutex);

#ifdef OPK_FRAME_ACCESS
    // Live endpoint/profile changes would invalidate what connected clients discovered.
    if (self->priv->started && (self->method == GST_OPK_COMM_METHOD_ICEORYX2 ||
                                (static_cast<PropertyId>(propertyId) == PropertyId::Method &&
                                 g_value_get_enum(value) == GST_OPK_COMM_METHOD_ICEORYX2) ||
                                static_cast<PropertyId>(propertyId) == PropertyId::FrameEndpoint ||
                                static_cast<PropertyId>(propertyId) == PropertyId::FrameProfile ||
                                static_cast<PropertyId>(propertyId) == PropertyId::RuntimePath)) {
        propertyLock.unlock();
        GST_WARNING_OBJECT(self, "Frame-access configuration cannot change while started");
        return;
    }
#endif

    switch (static_cast<PropertyId>(propertyId)) {
    case PropertyId::Method:
        self->method = static_cast<GstOpkCommMethod>(g_value_get_enum(value));
        break;

    case PropertyId::FileName:
        g_free(self->file_name);
        self->file_name = g_value_dup_string(value);
        if (self->file_name == nullptr) {
            self->file_name = g_strdup("");
        }
        break;

    case PropertyId::WsPort:
        self->ws_port = g_value_get_uint(value);
        break;

    case PropertyId::Endpoint:
        g_free(self->endpoint);
        self->endpoint = g_value_dup_string(value);
        if (self->endpoint == nullptr) {
            self->endpoint = g_strdup("/ws");
        }
        break;

    case PropertyId::TcpHost:
        g_free(self->tcp_host);
        self->tcp_host = g_value_dup_string(value);
        if (self->tcp_host == nullptr) {
            self->tcp_host = g_strdup("127.0.0.1");
        }
        break;

    case PropertyId::TcpPort:
        self->tcp_port = g_value_get_uint(value);
        break;
#ifdef OPK_FRAME_ACCESS
    case PropertyId::FrameEndpoint:
        self->priv->frame_endpoint = g_value_get_string(value) ? g_value_get_string(value) : "";
        break;
    case PropertyId::FrameProfile:
        self->priv->frame_profile = g_value_get_string(value) ? g_value_get_string(value) : "";
        break;
    case PropertyId::RuntimePath:
        self->priv->runtime_path = g_value_get_string(value) ? g_value_get_string(value) : "";
        break;
#endif

    default:
        propertyLock.unlock();
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, propertySpec);
        return;
    }

    propertyLock.unlock();

    if (self->priv->started && !gst_opk_comm_open_io(self)) {
        g_autofree gchar *failureDetails = gst_opk_comm_open_failure_details(self);
        // A synchronous bus callback may stop the pipeline and wait for streaming.
        lock.unlock();
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          OPEN_WRITE,
                          ("Failed to open opkcomm metadata writer"),
                          ("%s", failureDetails));
    }
}

static void gst_opk_comm_get_property(GObject *object,
                                      guint propertyId,
                                      GValue *value,
                                      GParamSpec *propertySpec) {
    const GstOpkComm *self = GST_OPK_COMM(object);
    const std::scoped_lock lock(self->priv->property_mutex);

    switch (static_cast<PropertyId>(propertyId)) {
    case PropertyId::Method:
        g_value_set_enum(value, self->method);
        break;

    case PropertyId::FileName:
        g_value_set_string(value, self->file_name);
        break;

    case PropertyId::WsPort:
        g_value_set_uint(value, self->ws_port);
        break;

    case PropertyId::Endpoint:
        g_value_set_string(value, self->endpoint);
        break;

    case PropertyId::TcpHost:
        g_value_set_string(value, self->tcp_host);
        break;

    case PropertyId::TcpPort:
        g_value_set_uint(value, self->tcp_port);
        break;
#ifdef OPK_FRAME_ACCESS
    case PropertyId::FrameEndpoint:
        g_value_set_string(value, self->priv->frame_endpoint.c_str());
        break;
    case PropertyId::FrameProfile:
        g_value_set_string(value, self->priv->frame_profile.c_str());
        break;
    case PropertyId::RuntimePath:
        g_value_set_string(value, self->priv->runtime_path.c_str());
        break;
    case PropertyId::FrameReady:
        g_value_set_boolean(value, self->priv->frame_ready.load());
        break;
#endif

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, propertySpec);
        break;
    }
}

static void gst_opk_comm_finalize(GObject *object) {
    GstOpkComm *self = GST_OPK_COMM(object);

    gst_opk_comm_close_io(self);

    delete self->priv;

    g_clear_pointer(&self->file_name, g_free);
    g_clear_pointer(&self->endpoint, g_free);
    g_clear_pointer(&self->tcp_host, g_free);

    G_OBJECT_CLASS(gst_opk_comm_parent_class)->finalize(object);
}

static void gst_opk_comm_class_init(GstOpkCommClass *self) {
    auto *objectClass = G_OBJECT_CLASS(self);
    auto *elementClass = GST_ELEMENT_CLASS(self);
    auto *baseTransformClass = GST_BASE_TRANSFORM_CLASS(self);

    constexpr auto livePropertyFlags = static_cast<GParamFlags>(
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);

    objectClass->set_property = gst_opk_comm_set_property;
    objectClass->get_property = gst_opk_comm_get_property;
    objectClass->finalize = gst_opk_comm_finalize;

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::Method),
        g_param_spec_enum("method",
                          "Method",
                          "Publishing method: file, websocket, tcp, or optional iceoryx2"
                          "; Changeable while playing; synchronous reopen disconnects clients and "
                          "may discard queued output",
                          gst_opk_comm_method_get_type(),
                          GST_OPK_COMM_METHOD_FILE,
                          livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::FileName),
        g_param_spec_string("file-name",
                            "File name",
                            "File path ('-' - std out)"
                            "; Changeable while playing; synchronous reopen disconnects clients "
                            "and may discard queued output",
                            "-",
                            livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::WsPort),
        g_param_spec_uint("ws-port",
                          "WebSocket port",
                          "Port used when method=websocket"
                          "; Changeable while playing; synchronous reopen disconnects clients and "
                          "may discard queued output",
                          1,
                          65535,
                          8002,
                          livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::Endpoint),
        g_param_spec_string("endpoint",
                            "WebSocket endpoint",
                            "Endpoint path used when method=websocket"
                            "; Changeable while playing; synchronous reopen disconnects clients "
                            "and may discard queued output",
                            "/ws",
                            livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::TcpHost),
        g_param_spec_string("tcp-host",
                            "TCP host",
                            "Bind host used when method=tcp"
                            "; Changeable while playing; synchronous reopen disconnects clients "
                            "and may discard queued output",
                            "127.0.0.1",
                            livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::TcpPort),
        g_param_spec_uint("tcp-port",
                          "TCP port",
                          "Listen port used when method=tcp"
                          "; Changeable while playing; synchronous reopen disconnects clients and "
                          "may discard queued output",
                          1,
                          65535,
                          7001,
                          livePropertyFlags));

#ifdef OPK_FRAME_ACCESS
    constexpr auto frameFlags = static_cast<GParamFlags>(
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);
    g_object_class_install_property(objectClass,
                                    static_cast<guint>(PropertyId::FrameEndpoint),
                                    g_param_spec_string("frame-endpoint",
                                                        "Frame endpoint",
                                                        "Explicit native frame-access service name",
                                                        nullptr,
                                                        frameFlags));
    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::FrameProfile),
        g_param_spec_string("frame-profile",
                            "Frame profile",
                            "JSON with six explicit frame-access capacity limits",
                            nullptr,
                            frameFlags));
    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::RuntimePath),
        g_param_spec_string("runtime-path",
                            "Runtime path",
                            "Native runtime directory; unset uses the middleware default",
                            nullptr,
                            frameFlags));
    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::FrameReady),
        g_param_spec_boolean("frame-access-ready",
                             "Frame access ready",
                             "Native endpoint has negotiated caps and validated capacity",
                             FALSE,
                             static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    baseTransformClass->set_caps = gst_opk_comm_set_caps;
#endif

    /* Add pad templates so the element has sink/src pads */
    gst_element_class_add_pad_template(elementClass, gst_static_pad_template_get(&sink_template));

    gst_element_class_add_pad_template(elementClass, gst_static_pad_template_get(&src_template));

    /* We are a pure passthrough transform. */
    baseTransformClass->start = gst_opk_comm_start;
    baseTransformClass->stop = gst_opk_comm_stop;
    baseTransformClass->transform_ip = gst_opk_comm_transform_ip;

    gst_element_class_set_static_metadata(
        elementClass,
        "OpkComm metadata publisher",
        "Filter/Metadata",
        "Reads buffer metadata and publishes it (FIFO/file/WebSocket/TCP)",
        "Arm Limited <perception-fdbck@arm.com>");
}

static void gst_opk_comm_init(GstOpkComm *self) {
    self->method = GST_OPK_COMM_METHOD_FILE;
    self->file_name = g_strdup("-");
    self->ws_port = 8002;
    self->endpoint = g_strdup("/ws");
    self->tcp_host = g_strdup("127.0.0.1");
    self->tcp_port = 7001;
    self->frame_counter = 0L;

    self->priv = new GstOpkCommPrivate{};

    /* In-place capable (BaseTransform may still copy if buffer isn't writable). */
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), TRUE);
}

/* ----------------------- Plugin entry ----------------------- */

static gboolean opkcomm_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "opkcomm", GST_RANK_NONE, GST_TYPE_OPK_COMM);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opkcomm,
                  "OpkComm metadata publisher",
                  opkcomm_plugin_init,
                  "0.1.0",
                  "Apache 2.0",
                  PACKAGE,
                  "https://www.arm.com/")
