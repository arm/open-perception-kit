/* Build:
g++ -fPIC -shared -o libgstampsink.so ampsink.cpp \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
*/

#include <gst/gst.h>
#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "amp/Tools.h"

#ifndef PACKAGE
#define PACKAGE "ampsink"
#endif

/* =============================== AmpSink ============================== */

typedef struct _GstAmpSink      GstAmpSink;
typedef struct _GstAmpSinkClass GstAmpSinkClass;

struct _GstAmpSink {
  GstBin parent;

  /* video chain */
  GstElement *vconv, *x264, *h264parse, *vq, *vseg;

  /* mux + sink */
  GstElement *mux, *udpsink;

  /* audio chain (created on first audiopad request) */
  GstElement *aconv, *ares, *acaps, *arate, *audioenc, *audioparse, *aq, *aseg;

  /* properties */
  gchar     *host;
  gint       port;
  gboolean   sync;
  gboolean   async;

  gchar     *video_tune;           /* e.g., "zerolatency" */
  gchar     *video_speed_preset;   /* e.g., "ultrafast"   */
  gint       video_keyint;         /* GOP size            */

  gint       audio_bitrate;        /* encoder bitrate (bps) */
};

struct _GstAmpSinkClass {
  GstBinClass parent_class;
};

GType gst_amp_sink_get_type (void);
#define GST_TYPE_AMP_SINK (gst_amp_sink_get_type())
G_DEFINE_TYPE(GstAmpSink, gst_amp_sink, GST_TYPE_BIN)

/* ===== Utils ===== */

static gboolean
have_element(const char *name) {
  GstElementFactory *f = gst_element_factory_find(name);
  if (f) { gst_object_unref(f); return TRUE; }
  return FALSE;
}

static void
set_int_if_prop_exists(GstElement *e, const char *prop, gint value) {
  if (!e) return;
  GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
  if (ps) g_object_set(e, prop, value, NULL);
}

static void push_props_down(GstAmpSink *self) {
  if (self->udpsink) {
    g_object_set(self->udpsink,
                //  "host",  self->host ? self->host : (gchar*)"127.0.0.1",
                 "host",  self->host ? self->host : (gchar*)amp::Tools::getLocalIp().c_str(),
                 "port",  self->port,
                 "sync",  self->sync,
                 "async", self->async,
                 NULL);
  }
  if (self->x264) {
    if (self->video_tune)
      gst_util_set_object_arg(G_OBJECT(self->x264), "tune", self->video_tune);
    if (self->video_speed_preset)
      gst_util_set_object_arg(G_OBJECT(self->x264), "speed-preset", self->video_speed_preset);
    if (self->video_keyint >= 0)
      g_object_set(self->x264, "key-int-max", self->video_keyint, NULL);
  }
  if (self->audioenc && self->audio_bitrate > 0) {
    /* avenc_mp2 uses 'bitrate' (bps). twolame typically uses kbps. Try both. */
    set_int_if_prop_exists(self->audioenc, "bitrate", self->audio_bitrate);            /* bps */
    set_int_if_prop_exists(self->audioenc, "bitrate-kbps", self->audio_bitrate / 1000);/* kbps */
  }
}

/* ===== Properties ===== */
enum {
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_SYNC,
  PROP_ASYNC,
  PROP_VIDEO_TUNE,
  PROP_VIDEO_SPEED_PRESET,
  PROP_VIDEO_KEY_INT_MAX,
  PROP_AUDIO_BITRATE
};

static void gst_amp_sink_set_property(GObject *object, guint prop_id,
                                      const GValue *value, GParamSpec *pspec) {
  auto *self = reinterpret_cast<GstAmpSink*>(object);
  switch (prop_id) {
    case PROP_HOST:               g_free(self->host); self->host = g_value_dup_string(value); break;
    case PROP_PORT:               self->port = g_value_get_int(value); break;
    case PROP_SYNC:               self->sync = g_value_get_boolean(value); break;
    case PROP_ASYNC:              self->async = g_value_get_boolean(value); break;
    case PROP_VIDEO_TUNE:         g_free(self->video_tune); self->video_tune = g_value_dup_string(value); break;
    case PROP_VIDEO_SPEED_PRESET: g_free(self->video_speed_preset); self->video_speed_preset = g_value_dup_string(value); break;
    case PROP_VIDEO_KEY_INT_MAX:  self->video_keyint = g_value_get_int(value); break;
    case PROP_AUDIO_BITRATE:      self->audio_bitrate = g_value_get_int(value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec); return;
  }
  push_props_down(self);
}

static void gst_amp_sink_get_property(GObject *object, guint prop_id,
                                      GValue *value, GParamSpec *pspec) {
  auto *self = reinterpret_cast<GstAmpSink*>(object);
  switch (prop_id) {
    case PROP_HOST:               g_value_set_string (value, self->host); break;
    case PROP_PORT:               g_value_set_int    (value, self->port); break;
    case PROP_SYNC:               g_value_set_boolean(value, self->sync); break;
    case PROP_ASYNC:              g_value_set_boolean(value, self->async); break;
    case PROP_VIDEO_TUNE:         g_value_set_string (value, self->video_tune); break;
    case PROP_VIDEO_SPEED_PRESET: g_value_set_string (value, self->video_speed_preset); break;
    case PROP_VIDEO_KEY_INT_MAX:  g_value_set_int    (value, self->video_keyint); break;
    case PROP_AUDIO_BITRATE:      g_value_set_int    (value, self->audio_bitrate); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

/* ===== Pad templates ===== */
static GstStaticPadTemplate v_sink_template =
  GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate a_sink_template =
  GST_STATIC_PAD_TEMPLATE("audiopad",
    GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS("audio/x-raw"));

/* ===== Request/Release pads (audio, MP2 path + audiorate) ===== */

static GstElement* make_mp2_encoder(void) {
  /* Prefer avenc_mp2 (libav). Fallback to twolame if present. */
  GstElement *e = NULL;
  if (have_element("avenc_mp2"))    e = gst_element_factory_make("avenc_mp2", NULL);
  else if (have_element("twolame")) e = gst_element_factory_make("twolame", NULL);
  return e;
}

static GstPad *gst_amp_sink_request_new_pad(GstElement *element,
                                            GstPadTemplate *templ,
                                            const gchar *name,
                                            const GstCaps *caps) {
  auto *self = reinterpret_cast<GstAmpSink*>(element);
  const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE(templ);
  if (g_strcmp0(templ_name, "audiopad") != 0)
    return nullptr;

  /* Create audio chain on demand (only once) */
  if (!self->aconv) {
    self->aconv      = gst_element_factory_make("audioconvert", NULL);
    self->ares       = gst_element_factory_make("audioresample", NULL);
    self->acaps      = gst_element_factory_make("capsfilter", NULL);
    self->arate      = gst_element_factory_make("audiorate", NULL);
    self->audioenc   = make_mp2_encoder();
    self->audioparse = gst_element_factory_make("mpegaudioparse", NULL);
    self->aq         = gst_element_factory_make("queue", NULL);
    self->aseg       = gst_element_factory_make("identity", NULL); /* flatten segments */

    if (!self->aconv || !self->ares || !self->acaps || !self->arate ||
        !self->audioenc || !self->audioparse || !self->aq || !self->aseg) {
      GST_ERROR_OBJECT(self, "Audio chain elements missing (need audioconvert, audioresample, capsfilter, audiorate, MP2 encoder, mpegaudioparse, queue, identity)");
      return NULL;
    }

    /* Normalize raw audio for encoder: S16LE, 48k, stereo */
    {
      GstCaps* norm = gst_caps_from_string(
        "audio/x-raw,format=S16LE,rate=48000,channels=2,layout=interleaved");
      g_object_set(self->acaps, "caps", norm, NULL);
      gst_caps_unref(norm);
    }

    /* audiorate: steady timestamps from the first buffer */
    g_object_set(self->arate,
                 "skip-to-first", TRUE,
                 "tolerance",     (gint64) 20000000 /* 20ms */,
                 NULL);

    /* Queue & timing */
    g_object_set(self->aq,
                 "max-size-buffers", 0,
                 "max-size-bytes",   0,
                 "max-size-time",    (guint64)(2 * GST_SECOND),
                 NULL);
    g_object_set(self->aseg, "single-segment", TRUE, NULL);

    /* Add and link: aconv → ares → acaps → audiorate → audioenc → audioparse → aq → aseg → mux */
    gst_bin_add_many(GST_BIN(self),
                     self->aconv, self->ares, self->acaps, self->arate,
                     self->audioenc, self->audioparse, self->aq, self->aseg, NULL);

    if (!gst_element_link_many(self->aconv, self->ares, self->acaps, self->arate,
                               self->audioenc, self->audioparse, self->aq, self->aseg, self->mux, NULL)) {
      GST_ERROR_OBJECT(self, "Failed to link audio chain (MP2 + audiorate)");
      return NULL;
    }

    /* Apply bitrate now that encoder exists */
    push_props_down(self);

    /* Sync states with parent */
    gst_element_sync_state_with_parent(self->aconv);
    gst_element_sync_state_with_parent(self->ares);
    gst_element_sync_state_with_parent(self->acaps);
    gst_element_sync_state_with_parent(self->arate);
    gst_element_sync_state_with_parent(self->audioenc);
    gst_element_sync_state_with_parent(self->audioparse);
    gst_element_sync_state_with_parent(self->aq);
    gst_element_sync_state_with_parent(self->aseg);
  }

  /* Ghost pad mapping external "audiopad" → aconv:sink */
  GstPad *as = gst_element_get_static_pad(self->aconv, "sink");
  GstPad *ag = gst_ghost_pad_new(name ? name : "audiopag", as);
  gst_object_unref(as);
  gst_element_add_pad(GST_ELEMENT(self), ag);
  return ag;
}

static void gst_amp_sink_release_pad(GstElement *element, GstPad *pad) {
  gst_element_remove_pad(element, pad);
}

/* ===== Lifecycle ===== */
static void gst_amp_sink_dispose(GObject *object) {
  auto *self = reinterpret_cast<GstAmpSink*>(object);
  g_clear_pointer(&self->host, g_free);
  g_clear_pointer(&self->video_tune, g_free);
  g_clear_pointer(&self->video_speed_preset, g_free);
  G_OBJECT_CLASS(gst_amp_sink_parent_class)->dispose(object);
}

static void gst_amp_sink_init(GstAmpSink *self) {
  /* defaults */
  self->host               = g_strdup(amp::Tools::getLocalIp().c_str());
  self->port               = 5000;
  self->sync               = FALSE;   /* sender shouldn't schedule */
  self->async              = FALSE;
  self->video_tune         = g_strdup("zerolatency");
  self->video_speed_preset = g_strdup("ultrafast");
  self->video_keyint       = 30;
  self->audio_bitrate      = 128000;

  /* mandatory children */
  self->vconv     = gst_element_factory_make("videoconvert", NULL);
  self->x264      = gst_element_factory_make("x264enc", NULL);
  self->h264parse = gst_element_factory_make("h264parse", NULL);
  self->vq        = gst_element_factory_make("queue", NULL);
  self->vseg      = gst_element_factory_make("identity", NULL); /* flatten segments */
  self->mux       = gst_element_factory_make("mpegtsmux", "ampsink-mux");
  self->udpsink   = gst_element_factory_make("udpsink", NULL);

  g_return_if_fail(self->vconv && self->x264 && self->h264parse && self->vq && self->vseg && self->mux && self->udpsink);

  /* configure x264 & video path */
  gst_util_set_object_arg(G_OBJECT(self->x264), "tune", "zerolatency");
  if (self->video_speed_preset) gst_util_set_object_arg(G_OBJECT(self->x264), "speed-preset", self->video_speed_preset);
  g_object_set(self->x264, "key-int-max", self->video_keyint, NULL);
  g_object_set(self->h264parse, "config-interval", 1, "alignment", 2 /* AU */, NULL);
  g_object_set(self->vq,
               "max-size-buffers", 0,
               "max-size-bytes",   0,
               "max-size-time",    (guint64)(2 * GST_SECOND),
               NULL);
  g_object_set(self->vseg, "single-segment", TRUE, NULL);

  /* aggressive TS tables/clock for fast lock */
  g_object_set(self->mux,
               "program-number", 1,
               "pmt-pid",        100,
               "pat-interval",   50,   /* ms */
               "pmt-interval",   50,   /* ms */
               "pcr-interval",   3,    /* packets */
               "alignment",      7,    /* access-unit */
               NULL);

  /* add & link video → queue → vseg → mux → udpsink */
  gst_bin_add_many(GST_BIN(self),
                   self->vconv, self->x264, self->h264parse, self->vq, self->vseg,
                   self->mux, self->udpsink, NULL);

  if (!gst_element_link_many(self->vconv, self->x264, self->h264parse, self->vq, self->vseg, self->mux, NULL))
    GST_ERROR_OBJECT(self, "Failed to link video chain");
  if (!gst_element_link(self->mux, self->udpsink))
    GST_ERROR_OBJECT(self, "Failed to link mux->udpsink");

  /* expose ALWAYS video ghost pad */
  {
    GstPad *vs = gst_element_get_static_pad(self->vconv, "sink");
    GstPad *vg = gst_ghost_pad_new("sink", vs);
    gst_object_unref(vs);
    gst_element_add_pad(GST_ELEMENT(self), vg);
  }

  /* initial property push */
  push_props_down(self);
}

static void gst_amp_sink_class_init(GstAmpSinkClass *klass) {
  auto *gobject_class = G_OBJECT_CLASS(klass);
  auto *element_class = GST_ELEMENT_CLASS(klass);

  gobject_class->set_property = gst_amp_sink_set_property;
  gobject_class->get_property = gst_amp_sink_get_property;
  gobject_class->dispose      = gst_amp_sink_dispose;

  /* C++ flags helper */
  constexpr GParamFlags kRW =
      static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /* properties */
  g_object_class_install_property(gobject_class, PROP_HOST,
    g_param_spec_string("host", "Host", "Destination host/IP for UDP sink",
                        "127.0.0.1", kRW));
  g_object_class_install_property(gobject_class, PROP_PORT,
    g_param_spec_int("port", "Port", "Destination UDP port",
                     1, 65535, 5000, kRW));
  g_object_class_install_property(gobject_class, PROP_SYNC,
    g_param_spec_boolean("sync", "Sync", "Synchronize on the clock (udpsink)",
                         FALSE, kRW));
  g_object_class_install_property(gobject_class, PROP_ASYNC,
    g_param_spec_boolean("async", "Async", "Asynchronous state change on udpsink",
                         FALSE, kRW));
  g_object_class_install_property(gobject_class, PROP_VIDEO_TUNE,
    g_param_spec_string("video-tune", "Video tune",
                        "x264enc tune (e.g., 'zerolatency')",
                        "zerolatency", kRW));
  g_object_class_install_property(gobject_class, PROP_VIDEO_SPEED_PRESET,
    g_param_spec_string("video-speed-preset", "Video speed preset",
                        "x264enc speed-preset (e.g., 'ultrafast')",
                        "ultrafast", kRW));
  g_object_class_install_property(gobject_class, PROP_VIDEO_KEY_INT_MAX,
    g_param_spec_int("video-key-int-max", "Video keyint max",
                     "x264enc key-int-max (GOP size)",
                     0, 1000, 30, kRW));
  g_object_class_install_property(gobject_class, PROP_AUDIO_BITRATE,
    g_param_spec_int("audio-bitrate", "Audio bitrate (bps)",
                     "Audio encoder bitrate in bits per second",
                     6000, 512000, 128000, kRW));

  /* pads */
  gst_element_class_add_static_pad_template(element_class, &v_sink_template);
  gst_element_class_add_static_pad_template(element_class, &a_sink_template);

  /* request/release handlers for audio */
  element_class->request_new_pad = gst_amp_sink_request_new_pad;
  element_class->release_pad     = gst_amp_sink_release_pad;

  gst_element_class_set_static_metadata(element_class,
    "AmpSink (video+audio → H.264/MP2 → MPEG-TS → UDP)",
    "Sink/Network/Bin",
    "Encodes & muxes raw video+audio to TS and sends to UDP",
    "Your Name <you@example.com>");
}

/* ===== Plugin boilerplate ===== */
static gboolean plugin_init(GstPlugin *plugin) {
  return gst_element_register(plugin, "ampsink", GST_RANK_NONE, GST_TYPE_AMP_SINK);
}

GST_PLUGIN_DEFINE(
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  ampsink,
  "AmpSink bin: raw video+audio -> H264/MP2 -> MPEG-TS -> UDP",
  plugin_init,
  "1.0",
  "LGPL",
  PACKAGE,
  "https://example.com"
)

