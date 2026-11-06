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
} GstOpkCommMethod;

struct GstOpkCommPrivate {
    std::recursive_mutex writer_mutex;
    // Property readback must remain available while writer shutdown joins logging threads.
    std::mutex property_mutex;
    std::unique_ptr<Writer> writer;
    bool started = false;
    bool writer_open = false;
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

enum class OpkCommProps : guint {
    PROP_0,
    PROP_METHOD,
    PROP_FILE_NAME,
    PROP_WS_PORT,
    PROP_ENDPOINT,
    PROP_TCP_HOST,
    PROP_TCP_PORT,
};

static GType gst_opk_comm_method_get_type(void) {
    static GType methodType = 0;
    static std::vector<GEnumValue> values = {
        {GST_OPK_COMM_METHOD_FILE, "file", "file"},
        {GST_OPK_COMM_METHOD_WEBSOCKET, "websocket", "websocket"},
        {GST_OPK_COMM_METHOD_TCP, "tcp", "tcp"},
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
}

/* ----------------------- GstBaseTransform vfuncs ----------------------- */

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
    const std::scoped_lock lock(self->priv->writer_mutex);

    if (!self->priv->writer_open)
        return GST_FLOW_ERROR;

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

    switch (static_cast<OpkCommProps>(propertyId)) {
    case OpkCommProps::PROP_METHOD:
        self->method = static_cast<GstOpkCommMethod>(g_value_get_enum(value));
        break;

    case OpkCommProps::PROP_FILE_NAME:
        g_free(self->file_name);
        self->file_name = g_value_dup_string(value);
        if (self->file_name == nullptr) {
            self->file_name = g_strdup("");
        }
        break;

    case OpkCommProps::PROP_WS_PORT:
        self->ws_port = g_value_get_uint(value);
        break;

    case OpkCommProps::PROP_ENDPOINT:
        g_free(self->endpoint);
        self->endpoint = g_value_dup_string(value);
        if (self->endpoint == nullptr) {
            self->endpoint = g_strdup("/ws");
        }
        break;

    case OpkCommProps::PROP_TCP_HOST:
        g_free(self->tcp_host);
        self->tcp_host = g_value_dup_string(value);
        if (self->tcp_host == nullptr) {
            self->tcp_host = g_strdup("127.0.0.1");
        }
        break;

    case OpkCommProps::PROP_TCP_PORT:
        self->tcp_port = g_value_get_uint(value);
        break;

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

    switch (static_cast<OpkCommProps>(propertyId)) {
    case OpkCommProps::PROP_METHOD:
        g_value_set_enum(value, self->method);
        break;

    case OpkCommProps::PROP_FILE_NAME:
        g_value_set_string(value, self->file_name);
        break;

    case OpkCommProps::PROP_WS_PORT:
        g_value_set_uint(value, self->ws_port);
        break;

    case OpkCommProps::PROP_ENDPOINT:
        g_value_set_string(value, self->endpoint);
        break;

    case OpkCommProps::PROP_TCP_HOST:
        g_value_set_string(value, self->tcp_host);
        break;

    case OpkCommProps::PROP_TCP_PORT:
        g_value_set_uint(value, self->tcp_port);
        break;

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
        static_cast<guint>(OpkCommProps::PROP_METHOD),
        g_param_spec_enum("method",
                          "Method",
                          "Publishing method: file, websocket, or tcp"
                          "; Changeable while playing; synchronous reopen disconnects clients and "
                          "may discard queued output",
                          gst_opk_comm_method_get_type(),
                          GST_OPK_COMM_METHOD_FILE,
                          livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(OpkCommProps::PROP_FILE_NAME),
        g_param_spec_string("file-name",
                            "File name",
                            "File path ('-' - std out)"
                            "; Changeable while playing; synchronous reopen disconnects clients "
                            "and may discard queued output",
                            "-",
                            livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(OpkCommProps::PROP_WS_PORT),
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
        static_cast<guint>(OpkCommProps::PROP_ENDPOINT),
        g_param_spec_string("endpoint",
                            "WebSocket endpoint",
                            "Endpoint path used when method=websocket"
                            "; Changeable while playing; synchronous reopen disconnects clients "
                            "and may discard queued output",
                            "/ws",
                            livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(OpkCommProps::PROP_TCP_HOST),
        g_param_spec_string("tcp-host",
                            "TCP host",
                            "Bind host used when method=tcp"
                            "; Changeable while playing; synchronous reopen disconnects clients "
                            "and may discard queued output",
                            "127.0.0.1",
                            livePropertyFlags));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(OpkCommProps::PROP_TCP_PORT),
        g_param_spec_uint("tcp-port",
                          "TCP port",
                          "Listen port used when method=tcp"
                          "; Changeable while playing; synchronous reopen disconnects clients and "
                          "may discard queued output",
                          1,
                          65535,
                          7001,
                          livePropertyFlags));

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
