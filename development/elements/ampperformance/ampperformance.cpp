#include <cairo.h>
#include <cstring>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
#include <iomanip>
#include <sstream>
#include <string>

#include "PerformanceTracer.h"

#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMP_PERFORMANCE (gst_amp_performance_get_type())
#define GST_AMP_PERFORMANCE(obj)                                                                   \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMP_PERFORMANCE, GstAmpPerformance))
#define GST_AMP_PERFORMANCE_CLASS(klass)                                                           \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMP_PERFORMANCE, GstAmpPerformanceClass))
#define GST_IS_AMP_PERFORMANCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMP_PERFORMANCE))
#define GST_IS_AMP_PERFORMANCE_CLASS(klass)                                                        \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMP_PERFORMANCE))

typedef struct _GstAmpPerformance GstAmpPerformance;
typedef struct _GstAmpPerformanceClass GstAmpPerformanceClass;

struct _GstAmpPerformance {
    GstVideoFilter videofilter;

    // Properties
    gint x_offset;
    gint y_offset;
    gdouble font_size;
    gchar *background_color;
    gchar *text_color;
    gdouble alpha;

    // Internal state
    guint frame_count;
    guint update_interval;

    // Cached overlay surface
    cairo_surface_t *overlay_cache;
    guint cache_width;
    guint cache_height;
    gboolean cache_dirty;
};

struct _GstAmpPerformanceClass {
    GstVideoFilterClass parent_class;
};

GType gst_amp_performance_get_type(void);

G_END_DECLS

GST_DEBUG_CATEGORY_STATIC(gst_amp_performance_debug);
#define GST_CAT_DEFAULT gst_amp_performance_debug

// Default values
#define DEFAULT_X_OFFSET 10
#define DEFAULT_Y_OFFSET 10
#define DEFAULT_FONT_SIZE 12.0
#define DEFAULT_BG_COLOR "#000000"
#define DEFAULT_TEXT_COLOR "#00FF00"
#define DEFAULT_ALPHA 0.85
#define DEFAULT_UPDATE_INTERVAL 5

// Property IDs
enum {
    PROP_0,
    PROP_X_OFFSET,
    PROP_Y_OFFSET,
    PROP_FONT_SIZE,
    PROP_BG_COLOR,
    PROP_TEXT_COLOR,
    PROP_ALPHA,
    PROP_UPDATE_INTERVAL
};

// Function prototypes
static void gst_amp_performance_set_property(GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec);
static void
gst_amp_performance_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_amp_performance_finalize(GObject *object);
static GstFlowReturn gst_amp_performance_transform_frame_ip(GstVideoFilter *filter,
                                                            GstVideoFrame *frame);

// Helper function to parse hex color
static void parse_hex_color(const char *hex, double *r, double *g, double *b) {
    unsigned int color = 0;
    if (hex && hex[0] == '#') {
        sscanf(hex + 1, "%x", &color);
    }
    *r = ((color >> 16) & 0xFF) / 255.0;
    *g = ((color >> 8) & 0xFF) / 255.0;
    *b = (color & 0xFF) / 255.0;
}

#define gst_amp_performance_parent_class parent_class
G_DEFINE_TYPE(GstAmpPerformance, gst_amp_performance, GST_TYPE_VIDEO_FILTER);

static void gst_amp_performance_class_init(GstAmpPerformanceClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *vfilter_class = GST_VIDEO_FILTER_CLASS(klass);

    gobject_class->set_property = gst_amp_performance_set_property;
    gobject_class->get_property = gst_amp_performance_get_property;
    gobject_class->finalize = gst_amp_performance_finalize;

    vfilter_class->transform_frame_ip = gst_amp_performance_transform_frame_ip;

    // Install properties
    g_object_class_install_property(
        gobject_class,
        PROP_X_OFFSET,
        g_param_spec_int("x-offset",
                         "X Offset",
                         "Horizontal offset in pixels",
                         0,
                         G_MAXINT,
                         DEFAULT_X_OFFSET,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_Y_OFFSET,
        g_param_spec_int("y-offset",
                         "Y Offset",
                         "Vertical offset in pixels",
                         0,
                         G_MAXINT,
                         DEFAULT_Y_OFFSET,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_FONT_SIZE,
        g_param_spec_double("font-size",
                            "Font Size",
                            "Font size in points",
                            6.0,
                            72.0,
                            DEFAULT_FONT_SIZE,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_BG_COLOR,
        g_param_spec_string("bg-color",
                            "Background Color",
                            "Background color (hex)",
                            DEFAULT_BG_COLOR,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_TEXT_COLOR,
        g_param_spec_string("text-color",
                            "Text Color",
                            "Text color (hex)",
                            DEFAULT_TEXT_COLOR,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_ALPHA,
        g_param_spec_double("alpha",
                            "Alpha",
                            "Background transparency (0=transparent, 1=opaque)",
                            0.0,
                            1.0,
                            DEFAULT_ALPHA,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_UPDATE_INTERVAL,
        g_param_spec_uint("update-interval",
                          "Update Interval",
                          "Update overlay every N frames",
                          1,
                          120,
                          DEFAULT_UPDATE_INTERVAL,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Set metadata
    gst_element_class_set_static_metadata(
        element_class,
        "AMP Performance Overlay",
        "Filter/Effect/Video",
        "Overlays real-time performance metrics from Performance Tracer",
        "AMP Team <amp@example.com>");

    // Set pad templates
    GstCaps *caps = gst_caps_from_string("video/x-raw, format=(string){RGB, RGBA, BGR, BGRA}");
    GstPadTemplate *src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    GstPadTemplate *sink_template =
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template(element_class, src_template);
    gst_element_class_add_pad_template(element_class, sink_template);
    gst_caps_unref(caps);
}

static void gst_amp_performance_init(GstAmpPerformance *self) {
    self->x_offset = DEFAULT_X_OFFSET;
    self->y_offset = DEFAULT_Y_OFFSET;
    self->font_size = DEFAULT_FONT_SIZE;
    self->background_color = g_strdup(DEFAULT_BG_COLOR);
    self->text_color = g_strdup(DEFAULT_TEXT_COLOR);
    self->alpha = DEFAULT_ALPHA;
    self->frame_count = 0;
    self->update_interval = DEFAULT_UPDATE_INTERVAL;
    self->overlay_cache = nullptr;
    self->cache_width = 0;
    self->cache_height = 0;
    self->cache_dirty = true;
}

static void gst_amp_performance_finalize(GObject *object) {
    GstAmpPerformance *self = GST_AMP_PERFORMANCE(object);

    if (self->overlay_cache) {
        cairo_surface_destroy(self->overlay_cache);
        self->overlay_cache = nullptr;
    }

    g_free(self->background_color);
    g_free(self->text_color);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_amp_performance_set_property(GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec) {
    GstAmpPerformance *self = GST_AMP_PERFORMANCE(object);

    switch (prop_id) {
    case PROP_X_OFFSET:
        self->x_offset = g_value_get_int(value);
        break;
    case PROP_Y_OFFSET:
        self->y_offset = g_value_get_int(value);
        break;
    case PROP_FONT_SIZE:
        self->font_size = g_value_get_double(value);
        break;
    case PROP_BG_COLOR:
        g_free(self->background_color);
        self->background_color = g_value_dup_string(value);
        break;
    case PROP_TEXT_COLOR:
        g_free(self->text_color);
        self->text_color = g_value_dup_string(value);
        break;
    case PROP_ALPHA:
        self->alpha = g_value_get_double(value);
        break;
    case PROP_UPDATE_INTERVAL:
        self->update_interval = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_amp_performance_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstAmpPerformance *self = GST_AMP_PERFORMANCE(object);

    switch (prop_id) {
    case PROP_X_OFFSET:
        g_value_set_int(value, self->x_offset);
        break;
    case PROP_Y_OFFSET:
        g_value_set_int(value, self->y_offset);
        break;
    case PROP_FONT_SIZE:
        g_value_set_double(value, self->font_size);
        break;
    case PROP_BG_COLOR:
        g_value_set_string(value, self->background_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_string(value, self->text_color);
        break;
    case PROP_ALPHA:
        g_value_set_double(value, self->alpha);
        break;
    case PROP_UPDATE_INTERVAL:
        g_value_set_uint(value, self->update_interval);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

// Helper function to render overlay to cached surface
static void render_overlay_cache(GstAmpPerformance *self) {
    // Get global tracer (fresh each time to ensure same instance as ampinfer)
    amp::PerformanceTracer *tracer = amp::getGlobalTracer();

    // Parse colors
    double bg_r, bg_g, bg_b;
    double text_r, text_g, text_b;
    parse_hex_color(self->background_color, &bg_r, &bg_g, &bg_b);
    parse_hex_color(self->text_color, &text_r, &text_g, &text_b);

    // Collect performance data
    struct MetricData {
        const char *name;
        const char *key;
    };

    MetricData metrics[] = {{"PreProc", "preprocessing"},
                            {"Inference", "inference"},
                            {"PostProc", "postprocessing"},
                            {"Frame", "frame_total"}};

    std::vector<std::string> lines;
    lines.push_back("═══ Performance Metrics ═══");

    for (const auto &metric : metrics) {
        const auto stats = tracer->getStats(metric.key);

        char line_buffer[64];
        if (stats.count > 0) {
            double avg = stats.avg_ms();
            double p95 = stats.p95_ms();
            snprintf(line_buffer,
                     sizeof(line_buffer),
                     "%-10s: %5.2fms (p95:%5.2fms)",
                     metric.name,
                     avg,
                     p95);
        } else {
            snprintf(
                line_buffer, sizeof(line_buffer), "%-10s: %s", metric.name, "  -- (waiting...)  ");
        }
        lines.push_back(std::string(line_buffer));
    }

    // Calculate FPS
    const auto frame_stats = tracer->getStats("frame_total");
    size_t frame_count_total = frame_stats.count;

    char fps_buffer[64];
    if (frame_count_total > 0) {
        double avg_frame_ms = frame_stats.avg_ms();
        double fps = avg_frame_ms > 0 ? 1000.0 / avg_frame_ms : 0;
        snprintf(fps_buffer, sizeof(fps_buffer), "FPS       : %6.1f", fps);
    } else {
        snprintf(fps_buffer, sizeof(fps_buffer), "FPS       : %s", "  -- (waiting...)  ");
    }
    lines.push_back(std::string(fps_buffer));
    lines.push_back("═══════════════════════════");

    // Create temporary surface for font measurement
    cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *temp_cr = cairo_create(temp_surface);
    cairo_select_font_face(temp_cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(temp_cr, self->font_size);

    // Calculate fixed dimensions
    cairo_text_extents_t extents;
    cairo_text_extents(temp_cr, "PreProc  : 99.99ms (p95:99.99ms)", &extents);
    double max_width = extents.width;
    double line_height = self->font_size * 1.5;
    double box_width = max_width + 30;
    double box_height = lines.size() * line_height + 20;

    cairo_destroy(temp_cr);
    cairo_surface_destroy(temp_surface);

    // Create or recreate cache surface with fixed size
    guint cache_w = (guint)(box_width + 4);
    guint cache_h = (guint)(box_height + 4);

    if (self->overlay_cache) {
        cairo_surface_destroy(self->overlay_cache);
    }

    self->overlay_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cache_w, cache_h);
    self->cache_width = cache_w;
    self->cache_height = cache_h;

    cairo_t *cr = cairo_create(self->overlay_cache);

    // Clear with transparency
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Draw semi-transparent background box
    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, self->alpha);
    cairo_rectangle(cr, 2, 2, box_width, box_height);
    cairo_fill(cr);

    // Draw border
    cairo_set_source_rgba(cr, text_r, text_g, text_b, self->alpha);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, 2, 2, box_width, box_height);
    cairo_stroke(cr);

    // Draw text
    cairo_set_source_rgb(cr, text_r, text_g, text_b);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, self->font_size);

    double y_pos = 22;
    for (const auto &line : lines) {
        cairo_move_to(cr, 12, y_pos);
        cairo_show_text(cr, line.c_str());
        y_pos += line_height;
    }

    cairo_destroy(cr);
    self->cache_dirty = false;
}

static GstFlowReturn gst_amp_performance_transform_frame_ip(GstVideoFilter *filter,
                                                            GstVideoFrame *frame) {
    GstAmpPerformance *self = GST_AMP_PERFORMANCE(filter);

    self->frame_count++;

    // Update cache every N frames
    if (self->cache_dirty || (self->frame_count % self->update_interval) == 0) {
        render_overlay_cache(self);
    }

    // Quick exit if no cache
    if (!self->overlay_cache) {
        return GST_FLOW_OK;
    }

    gint width = GST_VIDEO_FRAME_WIDTH(frame);
    gint height = GST_VIDEO_FRAME_HEIGHT(frame);
    gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
    guint8 *data = (guint8 *)GST_VIDEO_FRAME_PLANE_DATA(frame, 0);

    GstVideoFormat format = GST_VIDEO_FRAME_FORMAT(frame);
    cairo_format_t cairo_format;

    switch (format) {
    case GST_VIDEO_FORMAT_BGRA:
        cairo_format = CAIRO_FORMAT_ARGB32;
        break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
        cairo_format = CAIRO_FORMAT_RGB24;
        break;
    default:
        return GST_FLOW_OK;
    }

    // Create Cairo surface for video frame
    cairo_surface_t *surface =
        cairo_image_surface_create_for_data(data, cairo_format, width, height, stride);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return GST_FLOW_ERROR;
    }

    // Fast blit: just composite the cached overlay onto the frame
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_surface(cr, self->overlay_cache, self->x_offset, self->y_offset);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return GST_FLOW_OK;
}

// Plugin initialization
static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(
        gst_amp_performance_debug, "ampperformance", 0, "AMP Performance Overlay");

    return gst_element_register(plugin, "ampperformance", GST_RANK_NONE, GST_TYPE_AMP_PERFORMANCE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ampperformance,
                  "AMP Performance Overlay - displays real-time performance metrics",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
