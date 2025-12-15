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
    gboolean show_all_metrics;

    // Internal state
    guint frame_count;
    guint update_interval;

    // FPS tracking
    std::chrono::steady_clock::time_point last_frame_time;
    gdouble fps_average;

    // Cached overlay surface
    cairo_surface_t *overlay_cache;
    guint cache_width;
    guint cache_height;
    gboolean cache_dirty;

    // Track maximum height to prevent vertical flickering when metric count changes
    guint max_height;
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
#define DEFAULT_SHOW_ALL_METRICS FALSE

// Property IDs
enum {
    PROP_0,
    PROP_X_OFFSET,
    PROP_Y_OFFSET,
    PROP_FONT_SIZE,
    PROP_BG_COLOR,
    PROP_TEXT_COLOR,
    PROP_ALPHA,
    PROP_UPDATE_INTERVAL,
    PROP_SHOW_ALL_METRICS
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

    g_object_class_install_property(
        gobject_class,
        PROP_SHOW_ALL_METRICS,
        g_param_spec_boolean("show-all-metrics",
                             "Show All Metrics",
                             "Display all available metrics instead of predefined list",
                             DEFAULT_SHOW_ALL_METRICS,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Set metadata
    gst_element_class_set_static_metadata(
        element_class,
        "AMP Performance Overlay",
        "Filter/Effect/Video",
        "Overlays real-time performance metrics from Performance Tracer",
        "AMP Team <amp@example.com>");

    // Set pad templates
    GstCaps *caps = gst_caps_from_string("video/x-raw, format=(string){RGBA}");
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
    self->show_all_metrics = DEFAULT_SHOW_ALL_METRICS;
    self->last_frame_time = std::chrono::steady_clock::now();
    self->fps_average = 0.0;
    self->overlay_cache = nullptr;
    self->cache_width = 0;
    self->cache_height = 0;
    self->cache_dirty = true;
    self->max_height = 0;
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
    case PROP_SHOW_ALL_METRICS:
        self->show_all_metrics = g_value_get_boolean(value);
        self->cache_dirty = true;
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
    case PROP_SHOW_ALL_METRICS:
        g_value_set_boolean(value, self->show_all_metrics);
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

    // End the performance cycle to calculate statistics
    tracer->endCycle();

    // Parse colors
    double bg_r, bg_g, bg_b;
    double text_r, text_g, text_b;
    parse_hex_color(self->background_color, &bg_r, &bg_g, &bg_b);
    parse_hex_color(self->text_color, &text_r, &text_g, &text_b);

    // Collect performance data
    std::vector<std::string> lines;
    lines.push_back("═══ Performance Metrics ═══");

    // Group metrics by model name (prefix before underscore) - declared outside for FPS calculation
    std::map<std::string, std::vector<std::pair<std::string, amp::TimingStats>>> grouped_metrics;

    if (self->show_all_metrics) {
        // Display all available metrics from tracer, grouped by model
        auto all_stats = tracer->getAllStats();

        for (const auto &[key, stats] : all_stats) {
            if (stats.count > 0) {
                // Extract model name (everything before first underscore)
                size_t underscore_pos = key.find('_');
                std::string model_name =
                    (underscore_pos != std::string::npos) ? key.substr(0, underscore_pos) : key;
                grouped_metrics[model_name].push_back({key, stats});
            }
        }

        // Display metrics grouped by model, ordered: preprocess, inference, postprocess
        for (const auto &[model_name, metrics] : grouped_metrics) {
            // Sort metrics within each model: preprocess -> inference -> postprocess
            std::vector<std::pair<std::string, amp::TimingStats>> sorted_metrics = metrics;
            std::sort(
                sorted_metrics.begin(), sorted_metrics.end(), [](const auto &a, const auto &b) {
                    auto get_order = [](const std::string &key) {
                        if (key.find("_preprocess") != std::string::npos)
                            return 0;
                        if (key.find("_inference") != std::string::npos)
                            return 1;
                        if (key.find("_postprocess") != std::string::npos)
                            return 2;
                        return 3;
                    };
                    return get_order(a.first) < get_order(b.first);
                });

            for (const auto &[key, stats] : sorted_metrics) {
                char line_buffer[96];
                double avg = stats.avg_ms();
                double p95 = stats.p95_ms();
                snprintf(line_buffer,
                         sizeof(line_buffer),
                         "%-24s: %7.2fms  (p95: %7.2fms)",
                         key.c_str(),
                         avg,
                         p95);
                lines.push_back(std::string(line_buffer));
            }
        }
    } else {
        // Display predefined list of metrics
        struct MetricData {
            const char *name;
            const char *key;
        };

        MetricData metrics[] = {{"PreProc", "preprocessing"},
                                {"Inference", "inference"},
                                {"PostProc", "postprocessing"}};

        for (const auto &metric : metrics) {
            const auto stats = tracer->getStats(metric.key);

            char line_buffer[96];
            if (stats.count > 0) {
                double avg = stats.avg_ms();
                double p95 = stats.p95_ms();
                snprintf(line_buffer,
                         sizeof(line_buffer),
                         "%-24s: %7.2fms  (p95: %7.2fms)",
                         metric.name,
                         avg,
                         p95);
            } else {
                snprintf(line_buffer,
                         sizeof(line_buffer),
                         "%-24s: %s",
                         metric.name,
                         "  -- (waiting...)  ");
            }
            lines.push_back(std::string(line_buffer));
        }
    }

    // Calculate processing capability FPS from sum of all p50 metrics
    double total_processing_ms = 0.0;
    for (const auto &[model_name, metric_list] : grouped_metrics) {
        for (const auto &[metric_name, stats] : metric_list) {
            total_processing_ms += stats.p50_ms();
        }
    }
    double processing_fps =
        (total_processing_ms > 0)
            ? 1000.0 / total_processing_ms
            : 0.0; // Display both pipeline FPS (actual frame rate) and processing FPS (capability)
    if (self->fps_average > 0) {
        char fps_buffer[96];
        snprintf(
            fps_buffer, sizeof(fps_buffer), "Pipeline FPS            : %7.1f", self->fps_average);
        lines.push_back(std::string(fps_buffer));
    }

    if (processing_fps > 0) {
        char proc_fps_buffer[96];
        snprintf(proc_fps_buffer,
                 sizeof(proc_fps_buffer),
                 "Processing FPS (max)    : %7.1f",
                 processing_fps);
        lines.push_back(std::string(proc_fps_buffer));
    }

    lines.push_back("═══════════════════════════════════════════════");

    // Create temporary surface for font measurement
    cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *temp_cr = cairo_create(temp_surface);
    cairo_select_font_face(temp_cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(temp_cr, self->font_size);

    // Calculate dimensions - use sample text matching the actual format
    // This should accommodate the longest metric names with proper column alignment
    cairo_text_extents_t extents;
    cairo_text_extents(temp_cr, "ultraface_postprocess  :  999.99ms  (p95:  999.99ms)", &extents);
    double measured_width = extents.width;

    // Calculate actual maximum line width from content
    double max_content_width = measured_width;
    for (const auto &line : lines) {
        cairo_text_extents(temp_cr, line.c_str(), &extents);
        if (extents.width > max_content_width) {
            max_content_width = extents.width;
        }
    }

    double line_height = self->font_size * 1.5;
    double box_width = max_content_width + 40; // Extra padding for table-like appearance
    double box_height = lines.size() * line_height + 20;

    cairo_destroy(temp_cr);
    cairo_surface_destroy(temp_surface);

    // Calculate dimensions - width is stable due to fixed formatting, track max height only
    guint cache_w = (guint)(box_width + 4);
    guint desired_h = (guint)(box_height + 4);

    // Track maximum height to prevent vertical flickering when metric count changes
    if (desired_h > self->max_height) {
        self->max_height = desired_h;
    }
    guint cache_h = self->max_height;

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

    // Draw semi-transparent background box (use max dimensions for stable size)
    double display_width = cache_w - 4;
    double display_height = cache_h - 4;

    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, self->alpha);
    cairo_rectangle(cr, 2, 2, display_width, display_height);
    cairo_fill(cr);

    // Draw border
    cairo_set_source_rgba(cr, text_r, text_g, text_b, self->alpha);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, 2, 2, display_width, display_height);
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

    // Track frame timing for FPS calculation
    auto current_time = std::chrono::steady_clock::now();
    if (self->frame_count > 1) { // Skip first frame
        auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            current_time - self->last_frame_time);
        double frame_ms = frame_duration.count() / 1000.0;
        double instant_fps = frame_ms > 0 ? 1000.0 / frame_ms : 0;
        // Exponential moving average for smoother FPS display
        self->fps_average = (self->fps_average == 0.0)
                                ? instant_fps
                                : (self->fps_average * 0.95 + instant_fps * 0.05);
    }
    self->last_frame_time = current_time;
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
        auto ret = cairo_surface_status(surface);
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
