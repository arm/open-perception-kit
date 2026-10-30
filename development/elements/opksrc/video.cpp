#include "opksrc.h"

static GstStaticPadTemplate video_template = GST_STATIC_PAD_TEMPLATE(
    "video",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=BGRA, width=640, height=480, framerate=30/1"));

static GstPadProbeReturn discard_unlinked_video(GstPad *const pad, GstPadProbeInfo *, gpointer) {
    return gst_pad_is_linked(pad) ? GST_PAD_PROBE_OK : GST_PAD_PROBE_DROP;
}

static GstElement *
add_video_element(GstOpkSrc *const self, const gchar *const factory, const gchar *const name) {
    GstElement *const element = gst_element_factory_make(factory, name);
    if (!element) {
        GST_ERROR_OBJECT(self, "Failed to create %s", factory);
        return nullptr;
    }
    if (!gst_bin_add(GST_BIN(self), element)) {
        gst_object_unref(element);
        return nullptr;
    }
    return element;
}

bool gst_opk_src_init_video(GstOpkSrc *const self) {
    GstElement *const black = add_video_element(self, "videotestsrc", "video-black");
    self->video_selector = add_video_element(self, "input-selector", "video-selector");
    GstElement *const filter = add_video_element(self, "capsfilter", "video-output");
    if (!black || !self->video_selector || !filter) {
        return false;
    }
    self->video_depay = add_video_element(self, "rtpvp8depay", "video-depay");
    GstElement *const queue = add_video_element(self, "queue", "video-receive-queue");
    GstElement *const decoder = add_video_element(self, "vp8dec", "video-decoder");
    GstElement *const convert = add_video_element(self, "videoconvert", "video-convert");
    GstElement *const scale = add_video_element(self, "videoscale", "video-scale");
    GstElement *const rate = add_video_element(self, "videorate", "video-rate");
    GstElement *const receive_filter = add_video_element(self, "capsfilter", "video-received");
    if (!self->video_depay || !queue || !decoder || !convert || !scale || !rate ||
        !receive_filter) {
        return false;
    }
    g_object_set(rate, "drop-only", TRUE, nullptr);
    gst_opk_src_keep_receive_open(self->video_depay);
    gst_util_set_object_arg(G_OBJECT(black), "pattern", "black");
    g_object_set(black, "is-live", TRUE, nullptr);
    g_object_set(self->video_selector, "sync-streams", TRUE, nullptr);
    gst_util_set_object_arg(G_OBJECT(self->video_selector), "sync-mode", "clock");
    GstCaps *const caps = gst_static_pad_template_get_caps(&video_template);
    g_object_set(filter, "caps", caps, nullptr);
    g_object_set(receive_filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
    if (!gst_element_link_many(
            self->video_depay, queue, decoder, convert, scale, rate, receive_filter, nullptr)) {
        return false;
    }
    self->video_pad = gst_element_request_pad_simple(self->video_selector, "sink_%u");
    if (!self->video_pad)
        return false;
    GstPad *const received_src = gst_element_get_static_pad(receive_filter, "src");
    const auto received_link = gst_pad_link(received_src, self->video_pad);
    gst_object_unref(received_src);
    if (received_link != GST_PAD_LINK_OK)
        return false;

    self->black_pad = gst_element_request_pad_simple(self->video_selector, "sink_%u");
    if (!self->black_pad) {
        return false;
    }
    GstPad *const black_src = gst_element_get_static_pad(black, "src");
    const GstPadLinkReturn result = gst_pad_link(black_src, self->black_pad);
    gst_object_unref(black_src);
    if (result != GST_PAD_LINK_OK || !gst_element_link(self->video_selector, filter)) {
        GST_ERROR_OBJECT(self, "Failed to link black video branch");
        return false;
    }
    g_object_set(self->video_selector, "active-pad", self->black_pad, nullptr);

    GstPad *const target = gst_element_get_static_pad(filter, "src");
    GstPadTemplate *const pad_template = gst_static_pad_template_get(&video_template);
    GstPad *const ghost = gst_ghost_pad_new_from_template("video", target, pad_template);
    gst_object_unref(pad_template);
    gst_object_unref(target);
    if (!ghost) {
        return false;
    }
    gst_pad_add_probe(
        ghost,
        static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
        discard_unlinked_video,
        nullptr,
        nullptr);
    if (!gst_element_add_pad(GST_ELEMENT(self), ghost)) {
        gst_object_unref(ghost);
        return false;
    }
    return true;
}

void gst_opk_src_video_class_init(GstElementClass *const element_class) {
    gst_element_class_add_static_pad_template(element_class, &video_template);
}

void gst_opk_src_dispose_video(GstOpkSrc *const self) {
    if (self->video_pad) {
        gst_element_release_request_pad(self->video_selector, self->video_pad);
        gst_clear_object(&self->video_pad);
    }
    if (self->black_pad) {
        gst_element_release_request_pad(self->video_selector, self->black_pad);
        gst_clear_object(&self->black_pad);
    }
}

bool gst_opk_src_link_video(GstOpkSrc *const self, GstPad *const pad, const GstCaps *const caps) {
    GstCaps *const supported =
        gst_caps_from_string("application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000");
    const bool matches = caps && !gst_caps_is_any(caps) && !gst_caps_is_empty(caps) &&
                         gst_caps_is_subset(caps, supported);
    gst_caps_unref(supported);
    if (!matches)
        return false;
    GstPad *const sink = gst_element_get_static_pad(self->video_depay, "sink");
    GstPad *const previous = gst_pad_get_peer(sink);
    if (previous) {
        gst_pad_unlink(previous, sink);
        gst_object_unref(previous);
    }
    const auto result = gst_pad_link(pad, sink);
    gst_object_unref(sink);
    if (result != GST_PAD_LINK_OK) {
        GST_ELEMENT_ERROR(
            self, CORE, PAD, ("Failed to link WebRTC video"), ("Pad link result: %d", result));
    }
    return true;
}

void gst_opk_src_select_video(GstOpkSrc *const self, const bool received) {
    if (self->video_selector) {
        g_object_set(self->video_selector,
                     "active-pad",
                     received ? self->video_pad : self->black_pad,
                     nullptr);
    }
}
