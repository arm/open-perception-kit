/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <cstdint>
#include <glib.h>
#include <glibconfig.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

#include <fcntl.h>
#include <memory>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_writer.h"
#include "writer.h"

#include <gst/PerceptionMeta.h>

#ifndef PACKAGE
#define PACKAGE "ampcomm"
#endif

/* ----------------------- Element definition ----------------------- */

#define GST_TYPE_AMP_COMM (gst_amp_comm_get_type())
G_DECLARE_FINAL_TYPE(GstAmpComm, gst_amp_comm, GST, AMP_COMM, GstBaseTransform)

typedef enum {

    // can be extended: MQTT, REST, database, etc...
    GST_AMP_COMM_METHOD_FILE = 0,
} GstAmpCommMethod;

struct GstAmpCommPrivate {
    std::unique_ptr<Writer> writer;
};

struct _GstAmpComm {
    GstBaseTransform parent;

    GstAmpCommMethod method;
    gchar *file_name;

    uint64_t frame_counter;

    GstAmpCommPrivate *priv;
};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE(GstAmpComm, gst_amp_comm, GST_TYPE_BASE_TRANSFORM)

/* ----------------------- Properties ----------------------- */

enum {
    PROP_0,
    PROP_METHOD,
    PROP_FILE_NAME,
};

static GType gst_amp_comm_method_get_type(void) {
    static GType t = 0;
    static const GEnumValue values[] = {{GST_AMP_COMM_METHOD_FILE, "file", "file"},
                                        {0, NULL, NULL}};

    if (g_once_init_enter(&t)) {
        GType tmp = g_enum_register_static("GstAmpCommMethod", values);
        g_once_init_leave(&t, tmp);
    }
    return t;
}

/* ----------------------- Helpers ----------------------- */

static bool gst_amp_comm_open_io(GstAmpComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }

    auto ret = true;
    switch (self->method) {
    case GST_AMP_COMM_METHOD_FILE:
        self->priv->writer = std::make_unique<FileWriter>(self, self->file_name, 5);
        ret = self->priv->writer->start();

        break;
    default:
        // intentionally does nothing
        break;
    }

    return ret;
}

static void gst_amp_comm_close_io(GstAmpComm *self) {
    if (self->priv->writer) {
        self->priv->writer->stop();
    }
}
/* ----------------------- GstBaseTransform vfuncs ----------------------- */

static gboolean gst_amp_comm_start(GstBaseTransform *trans) {
    GstAmpComm *self = GST_AMP_COMM(trans);

    gst_amp_comm_open_io(self);

    return TRUE;
}

static gboolean gst_amp_comm_stop(GstBaseTransform *trans) {
    GstAmpComm *self = GST_AMP_COMM(trans);
    gst_amp_comm_close_io(self);

    return TRUE;
}

static GstFlowReturn gst_amp_comm_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstAmpComm *self = GST_AMP_COMM(trans);

    if (self->priv && self->priv->writer) {

        if (self->priv->writer->check_running()) {

            auto p = amp::PerceptionMeta::read(buf);
            AmpCommJob j = {
                .frame_counter = self->frame_counter,
                .perception = p,
            };

            self->priv->writer->send(std::move(j));
        }
    }

    ++self->frame_counter;

    /* passthrough: do not modify buffer */
    return GST_FLOW_OK;
}

/* ----------------------- GObject plumbing ----------------------- */

static void
gst_amp_comm_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstAmpComm *self = GST_AMP_COMM(object);

    switch (prop_id) {
    case PROP_METHOD:
        self->method = (GstAmpCommMethod)g_value_get_enum(value);
        /* Reopen on method change */
        (void)gst_amp_comm_open_io(self);
        break;

    case PROP_FILE_NAME:
        g_free(self->file_name);
        self->file_name = g_value_dup_string(value);
        if (self->file_name == nullptr) {
            self->file_name = g_strdup("");
        }

        /* Reopen on name change */
        (void)gst_amp_comm_open_io(self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_amp_comm_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstAmpComm *self = GST_AMP_COMM(object);

    switch (prop_id) {
    case PROP_METHOD:
        g_value_set_enum(value, self->method);
        break;

    case PROP_FILE_NAME:
        g_value_set_string(value, self->file_name);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_amp_comm_finalize(GObject *object) {
    GstAmpComm *self = GST_AMP_COMM(object);

    gst_amp_comm_close_io(self);

    delete self->priv;

    g_clear_pointer(&self->file_name, g_free);

    G_OBJECT_CLASS(gst_amp_comm_parent_class)->finalize(object);
}

static void gst_amp_comm_class_init(GstAmpCommClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS(klass);

    constexpr GParamFlags kRW =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = gst_amp_comm_set_property;
    gobject_class->get_property = gst_amp_comm_get_property;
    gobject_class->finalize = gst_amp_comm_finalize;

    g_object_class_install_property(gobject_class,
                                    PROP_METHOD,
                                    g_param_spec_enum("method",
                                                      "Method",
                                                      "Publishing method: file",
                                                      gst_amp_comm_method_get_type(),
                                                      GST_AMP_COMM_METHOD_FILE,
                                                      kRW));

    g_object_class_install_property(
        gobject_class,
        PROP_FILE_NAME,
        g_param_spec_string("file-name", "File name", "File path ('-' - std out)", "-", kRW));

    /* ✅ Add pad templates so the element has sink/src pads */
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

    /* We are a pure passthrough transform. */
    bt_class->start = gst_amp_comm_start;
    bt_class->stop = gst_amp_comm_stop;
    bt_class->transform_ip = gst_amp_comm_transform_ip;

    gst_element_class_set_static_metadata(element_class,
                                          "AmpComm metadata publisher",
                                          "Filter/Metadata",
                                          "Reads buffer metadata and publishes it (FIFO/file)",
                                          "Arm Holding");
}

static void gst_amp_comm_init(GstAmpComm *self) {
    self->method = GST_AMP_COMM_METHOD_FILE;
    self->file_name = g_strdup("-");
    self->frame_counter = 0l;

    self->priv = new GstAmpCommPrivate{};

    /* In-place capable (BaseTransform may still copy if buffer isn't writable). */
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), TRUE);
}

/* ----------------------- Plugin entry ----------------------- */

static gboolean ampcomm_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "ampcomm", GST_RANK_NONE, GST_TYPE_AMP_COMM);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ampcomm,
                  "AmpComm metadata publisher",
                  ampcomm_plugin_init,
                  "0.1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
