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
    static GType t = 0;
    static std::vector<GEnumValue> values = {
        {GST_OPK_COMM_METHOD_FILE, "file", "file"},
        {GST_OPK_COMM_METHOD_WEBSOCKET, "websocket", "websocket"},
        {GST_OPK_COMM_METHOD_TCP, "tcp", "tcp"},
        {0, nullptr, nullptr}};

    if (g_once_init_enter(&t)) {
        GType tmp = g_enum_register_static("GstOpkCommMethod", values.data());
        g_once_init_leave(&t, tmp);
    }
    return t;
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

static void gst_opk_comm_report_open_failure(GstOpkComm *self, bool fatal) {
    const char *method = gst_opk_comm_method_name(self->method);
    const char *endpoint = self->endpoint ? self->endpoint : "";
    const char *tcp_host = self->tcp_host ? self->tcp_host : "";
    const char *file_name = self->file_name ? self->file_name : "";

    if (fatal) {
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          OPEN_WRITE,
                          ("Failed to open opkcomm metadata writer"),
                          ("method=%s file-name='%s' ws-port=%u endpoint='%s' "
                           "tcp-host='%s' tcp-port=%u",
                           method,
                           file_name,
                           self->ws_port,
                           endpoint,
                           tcp_host,
                           self->tcp_port));
    } else {
        GST_ELEMENT_WARNING(self,
                            RESOURCE,
                            OPEN_WRITE,
                            ("Failed to reopen opkcomm metadata writer"),
                            ("method=%s file-name='%s' ws-port=%u endpoint='%s' "
                             "tcp-host='%s' tcp-port=%u",
                             method,
                             file_name,
                             self->ws_port,
                             endpoint,
                             tcp_host,
                             self->tcp_port));
    }
}

static bool gst_opk_comm_open_io(GstOpkComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }
    self->priv->writer_open = false;

    auto ret = false;
    switch (self->method) {
    case GST_OPK_COMM_METHOD_FILE:
        self->priv->writer = std::make_unique<FileWriter>(self, self->file_name, 5);
        ret = self->priv->writer->start();
        break;
    case GST_OPK_COMM_METHOD_WEBSOCKET:
        self->priv->writer = std::make_unique<WebSocketWriter>(
            self, static_cast<uint16_t>(self->ws_port), self->endpoint ? self->endpoint : "/ws", 5);
        ret = self->priv->writer->start();
        break;
    case GST_OPK_COMM_METHOD_TCP:
        self->priv->writer = std::make_unique<TcpWriter>(
            self, self->tcp_host ? self->tcp_host : "", static_cast<uint16_t>(self->tcp_port), 5);
        ret = self->priv->writer->start();
        break;
    default:
        GST_WARNING_OBJECT(
            self, "Unknown opkcomm publishing method: %d", static_cast<int>(self->method));
        break;
    }

    if (ret) {
        self->priv->writer_open = true;
    } else {
        self->priv->writer.reset();
    }

    return ret;
}

static void gst_opk_comm_close_io(GstOpkComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }
    self->priv->writer.reset();
    self->priv->writer_open = false;
}

static void gst_opk_comm_reopen_if_started(GstOpkComm *self) {
    if (!self->priv->started) {
        return;
    }

    if (!gst_opk_comm_open_io(self)) {
        gst_opk_comm_report_open_failure(self, false);
    }
}
/* ----------------------- GstBaseTransform vfuncs ----------------------- */

static gboolean gst_opk_comm_start(GstBaseTransform *trans) {
    GstOpkComm *self = GST_OPK_COMM(trans);

    self->priv->started = true;
    if (!gst_opk_comm_open_io(self)) {
        self->priv->started = false;
        gst_opk_comm_report_open_failure(self, true);
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_opk_comm_stop(GstBaseTransform *trans) {
    GstOpkComm *self = GST_OPK_COMM(trans);
    self->priv->started = false;
    gst_opk_comm_close_io(self);

    return TRUE;
}

static GstFlowReturn gst_opk_comm_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstOpkComm *self = GST_OPK_COMM(trans);

    if (self->priv && self->priv->writer_open && self->priv->writer &&
        self->priv->writer->check_running()) {
        auto frameResults = opk::FrameResultsMeta::read(buf);
        OpkCommJob j = {
            .frame_counter = self->frame_counter,
            .frameResults = frameResults,
        };

        self->priv->writer->send(std::move(j));
    }

    ++self->frame_counter;

    /* passthrough: do not modify buffer */
    return GST_FLOW_OK;
}

/* ----------------------- GObject plumbing ----------------------- */

static void
gst_opk_comm_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstOpkComm *self = GST_OPK_COMM(object);

    switch (OpkCommProps(prop_id)) {
    case OpkCommProps::PROP_METHOD:
        self->method = (GstOpkCommMethod)g_value_get_enum(value);

        /* Reopen on method change */
        gst_opk_comm_reopen_if_started(self);
        break;

    case OpkCommProps::PROP_FILE_NAME:
        g_free(self->file_name);
        self->file_name = g_value_dup_string(value);
        if (self->file_name == nullptr) {
            self->file_name = g_strdup("");
        }

        /* Reopen on name change */
        gst_opk_comm_reopen_if_started(self);
        break;

    case OpkCommProps::PROP_WS_PORT:
        self->ws_port = g_value_get_uint(value);
        gst_opk_comm_reopen_if_started(self);
        break;

    case OpkCommProps::PROP_ENDPOINT:
        g_free(self->endpoint);
        self->endpoint = g_value_dup_string(value);
        if (self->endpoint == nullptr) {
            self->endpoint = g_strdup("/ws");
        }
        gst_opk_comm_reopen_if_started(self);
        break;

    case OpkCommProps::PROP_TCP_HOST:
        g_free(self->tcp_host);
        self->tcp_host = g_value_dup_string(value);
        if (self->tcp_host == nullptr) {
            self->tcp_host = g_strdup("127.0.0.1");
        }
        gst_opk_comm_reopen_if_started(self);
        break;

    case OpkCommProps::PROP_TCP_PORT:
        self->tcp_port = g_value_get_uint(value);
        gst_opk_comm_reopen_if_started(self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_opk_comm_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    const GstOpkComm *self = GST_OPK_COMM(object);

    switch (OpkCommProps(prop_id)) {
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
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
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

static void gst_opk_comm_class_init(GstOpkCommClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);
    auto *bt_class = GST_BASE_TRANSFORM_CLASS(klass);

    constexpr auto kRW = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = gst_opk_comm_set_property;
    gobject_class->get_property = gst_opk_comm_get_property;
    gobject_class->finalize = gst_opk_comm_finalize;

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(OpkCommProps::PROP_METHOD),
                                    g_param_spec_enum("method",
                                                      "Method",
                                                      "Publishing method: file, websocket, or tcp",
                                                      gst_opk_comm_method_get_type(),
                                                      GST_OPK_COMM_METHOD_FILE,
                                                      kRW));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(OpkCommProps::PROP_FILE_NAME),
        g_param_spec_string("file-name", "File name", "File path ('-' - std out)", "-", kRW));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(OpkCommProps::PROP_WS_PORT),
        g_param_spec_uint(
            "ws-port", "WebSocket port", "Port used when method=websocket", 1, 65535, 8002, kRW));

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(OpkCommProps::PROP_ENDPOINT),
                                    g_param_spec_string("endpoint",
                                                        "WebSocket endpoint",
                                                        "Endpoint path used when method=websocket",
                                                        "/ws",
                                                        kRW));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(OpkCommProps::PROP_TCP_HOST),
        g_param_spec_string(
            "tcp-host", "TCP host", "Bind host used when method=tcp", "127.0.0.1", kRW));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(OpkCommProps::PROP_TCP_PORT),
        g_param_spec_uint(
            "tcp-port", "TCP port", "Listen port used when method=tcp", 1, 65535, 7001, kRW));

    /* Add pad templates so the element has sink/src pads */
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

    /* We are a pure passthrough transform. */
    bt_class->start = gst_opk_comm_start;
    bt_class->stop = gst_opk_comm_stop;
    bt_class->transform_ip = gst_opk_comm_transform_ip;

    gst_element_class_set_static_metadata(
        element_class,
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
                  "https://example.com")
