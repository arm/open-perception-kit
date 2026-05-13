/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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

#include <gst/PerceptionMeta.h>

#ifndef PACKAGE
constexpr const char *PACKAGE = "pekcomm";
#endif

/* ----------------------- Element definition ----------------------- */

#define GST_TYPE_PEK_COMM (gst_pek_comm_get_type())
G_DECLARE_FINAL_TYPE(GstPekComm, gst_pek_comm, GST, PEK_COMM, GstBaseTransform)

typedef enum {

    GST_PEK_COMM_METHOD_FILE = 1,
    GST_PEK_COMM_METHOD_WEBSOCKET = 2,
    GST_PEK_COMM_METHOD_TCP = 3,
} GstPekCommMethod;

struct GstPekCommPrivate {
    std::unique_ptr<Writer> writer;
};

struct _GstPekComm {
    GstBaseTransform parent;

    GstPekCommMethod method;
    gchar *file_name;
    guint ws_port;
    gchar *endpoint;
    gchar *tcp_host;
    guint tcp_port;

    uint64_t frame_counter;

    GstPekCommPrivate *priv;
};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE(GstPekComm, gst_pek_comm, GST_TYPE_BASE_TRANSFORM)

/* ----------------------- Properties ----------------------- */

enum class PekCommProps : guint {
    PROP_0,
    PROP_METHOD,
    PROP_FILE_NAME,
    PROP_WS_PORT,
    PROP_ENDPOINT,
    PROP_TCP_HOST,
    PROP_TCP_PORT,
};

static GType gst_pek_comm_method_get_type(void) {
    static GType t = 0;
    static std::vector<GEnumValue> values = {{GST_PEK_COMM_METHOD_FILE, "file", "file"},
                                             {GST_PEK_COMM_METHOD_WEBSOCKET, "websocket", "websocket"},
                                             {GST_PEK_COMM_METHOD_TCP, "tcp", "tcp"},
                                             {0, nullptr, nullptr}};

    if (g_once_init_enter(&t)) {
        GType tmp = g_enum_register_static("GstPekCommMethod", values.data());
        g_once_init_leave(&t, tmp);
    }
    return t;
}

/* ----------------------- Helpers ----------------------- */

static bool gst_pek_comm_open_io(GstPekComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }

    auto ret = true;
    switch (self->method) {
    case GST_PEK_COMM_METHOD_FILE:
        self->priv->writer = std::make_unique<FileWriter>(self, self->file_name, 5);
        ret = self->priv->writer->start();
        break;
    case GST_PEK_COMM_METHOD_WEBSOCKET:
        self->priv->writer = std::make_unique<WebSocketWriter>(
            self, static_cast<uint16_t>(self->ws_port), self->endpoint ? self->endpoint : "/ws", 5);
        ret = self->priv->writer->start();
        break;
    case GST_PEK_COMM_METHOD_TCP:
        self->priv->writer = std::make_unique<TcpWriter>(
            self, self->tcp_host ? self->tcp_host : "", static_cast<uint16_t>(self->tcp_port), 5);
        ret = self->priv->writer->start();
        break;
    default:
        // intentionally does nothing
        break;
    }

    return ret;
}

static void gst_pek_comm_close_io(GstPekComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }
}
/* ----------------------- GstBaseTransform vfuncs ----------------------- */

static gboolean gst_pek_comm_start(GstBaseTransform *trans) {
    GstPekComm *self = GST_PEK_COMM(trans);

    gst_pek_comm_open_io(self);

    return TRUE;
}

static gboolean gst_pek_comm_stop(GstBaseTransform *trans) {
    GstPekComm *self = GST_PEK_COMM(trans);
    gst_pek_comm_close_io(self);

    return TRUE;
}

static GstFlowReturn gst_pek_comm_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstPekComm *self = GST_PEK_COMM(trans);

    if (self->priv && self->priv->writer && self->priv->writer->check_running()) {
        auto p = pek::PerceptionMeta::read(buf);
        PekCommJob j = {
            .frame_counter = self->frame_counter,
            .perception = p,
        };

        self->priv->writer->send(std::move(j));
    }

    ++self->frame_counter;

    /* passthrough: do not modify buffer */
    return GST_FLOW_OK;
}

/* ----------------------- GObject plumbing ----------------------- */

static void
gst_pek_comm_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstPekComm *self = GST_PEK_COMM(object);

    switch (PekCommProps(prop_id)) {
    case PekCommProps::PROP_METHOD:
        self->method = (GstPekCommMethod)g_value_get_enum(value);

        /* Reopen on method change */
        (void)gst_pek_comm_open_io(self);
        break;

    case PekCommProps::PROP_FILE_NAME:
        g_free(self->file_name);
        self->file_name = g_value_dup_string(value);
        if (self->file_name == nullptr) {
            self->file_name = g_strdup("");
        }

        /* Reopen on name change */
        (void)gst_pek_comm_open_io(self);
        break;

    case PekCommProps::PROP_WS_PORT:
        self->ws_port = g_value_get_uint(value);
        (void)gst_pek_comm_open_io(self);
        break;

    case PekCommProps::PROP_ENDPOINT:
        g_free(self->endpoint);
        self->endpoint = g_value_dup_string(value);
        if (self->endpoint == nullptr) {
            self->endpoint = g_strdup("/ws");
        }
        (void)gst_pek_comm_open_io(self);
        break;

    case PekCommProps::PROP_TCP_HOST:
        g_free(self->tcp_host);
        self->tcp_host = g_value_dup_string(value);
        if (self->tcp_host == nullptr) {
            self->tcp_host = g_strdup("127.0.0.1");
        }
        (void)gst_pek_comm_open_io(self);
        break;

    case PekCommProps::PROP_TCP_PORT:
        self->tcp_port = g_value_get_uint(value);
        (void)gst_pek_comm_open_io(self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_pek_comm_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    const GstPekComm *self = GST_PEK_COMM(object);

    switch (PekCommProps(prop_id)) {
    case PekCommProps::PROP_METHOD:
        g_value_set_enum(value, self->method);
        break;

    case PekCommProps::PROP_FILE_NAME:
        g_value_set_string(value, self->file_name);
        break;

    case PekCommProps::PROP_WS_PORT:
        g_value_set_uint(value, self->ws_port);
        break;

    case PekCommProps::PROP_ENDPOINT:
        g_value_set_string(value, self->endpoint);
        break;

    case PekCommProps::PROP_TCP_HOST:
        g_value_set_string(value, self->tcp_host);
        break;

    case PekCommProps::PROP_TCP_PORT:
        g_value_set_uint(value, self->tcp_port);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_pek_comm_finalize(GObject *object) {
    GstPekComm *self = GST_PEK_COMM(object);

    gst_pek_comm_close_io(self);

    delete self->priv;

    g_clear_pointer(&self->file_name, g_free);
    g_clear_pointer(&self->endpoint, g_free);
    g_clear_pointer(&self->tcp_host, g_free);

    G_OBJECT_CLASS(gst_pek_comm_parent_class)->finalize(object);
}

static void gst_pek_comm_class_init(GstPekCommClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);
    auto *bt_class = GST_BASE_TRANSFORM_CLASS(klass);

    constexpr auto kRW = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = gst_pek_comm_set_property;
    gobject_class->get_property = gst_pek_comm_get_property;
    gobject_class->finalize = gst_pek_comm_finalize;

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(PekCommProps::PROP_METHOD),
                                    g_param_spec_enum("method",
                                                      "Method",
                                                      "Publishing method: file, websocket, or tcp",
                                                      gst_pek_comm_method_get_type(),
                                                      GST_PEK_COMM_METHOD_FILE,
                                                      kRW));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(PekCommProps::PROP_FILE_NAME),
        g_param_spec_string("file-name", "File name", "File path ('-' - std out)", "-", kRW));

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(PekCommProps::PROP_WS_PORT),
                                    g_param_spec_uint("ws-port",
                                                      "WebSocket port",
                                                      "Port used when method=websocket",
                                                      1,
                                                      65535,
                                                      8002,
                                                      kRW));

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(PekCommProps::PROP_ENDPOINT),
                                    g_param_spec_string("endpoint",
                                                        "WebSocket endpoint",
                                                        "Endpoint path used when method=websocket",
                                                        "/ws",
                                                        kRW));

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(PekCommProps::PROP_TCP_HOST),
                                    g_param_spec_string("tcp-host",
                                                        "TCP host",
                                                        "Bind host used when method=tcp",
                                                        "127.0.0.1",
                                                        kRW));

    g_object_class_install_property(gobject_class,
                                    static_cast<guint>(PekCommProps::PROP_TCP_PORT),
                                    g_param_spec_uint("tcp-port",
                                                      "TCP port",
                                                      "Listen port used when method=tcp",
                                                      1,
                                                      65535,
                                                      7001,
                                                      kRW));

    /* Add pad templates so the element has sink/src pads */
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

    /* We are a pure passthrough transform. */
    bt_class->start = gst_pek_comm_start;
    bt_class->stop = gst_pek_comm_stop;
    bt_class->transform_ip = gst_pek_comm_transform_ip;

    gst_element_class_set_static_metadata(element_class,
                                          "PekComm metadata publisher",
                                          "Filter/Metadata",
                                          "Reads buffer metadata and publishes it (FIFO/file/WebSocket/TCP)",
                                          "Arm Limited");
}

static void gst_pek_comm_init(GstPekComm *self) {
    self->method = GST_PEK_COMM_METHOD_FILE;
    self->file_name = g_strdup("-");
    self->ws_port = 8002;
    self->endpoint = g_strdup("/ws");
    self->tcp_host = g_strdup("127.0.0.1");
    self->tcp_port = 7001;
    self->frame_counter = 0L;

    self->priv = new GstPekCommPrivate{};

    /* In-place capable (BaseTransform may still copy if buffer isn't writable). */
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), TRUE);
}

/* ----------------------- Plugin entry ----------------------- */

static gboolean pekcomm_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "pekcomm", GST_RANK_NONE, GST_TYPE_PEK_COMM);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  pekcomm,
                  "PekComm metadata publisher",
                  pekcomm_plugin_init,
                  "0.1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
