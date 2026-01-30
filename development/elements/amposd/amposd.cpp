#include "gst/PerceptionContextMeta.h"
#include "osd.hpp"
#include <cmath>
#include <cstring>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
#include <new>

#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMP_OSD (gst_amp_osd_get_type())
#define GST_AMP_OSD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMP_OSD, GstAmpOsd))
#define GST_AMP_OSD_CLASS(klass)                                                                   \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMP_OSD, GstAmpOsdClass))
#define GST_IS_AMP_OSD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMP_OSD))
#define GST_IS_AMP_OSD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMP_OSD))

typedef struct _GstAmpOsd GstAmpOsd;
typedef struct _GstAmpOsdClass GstAmpOsdClass;

struct _GstAmpOsd {
    GstVideoFilter videofilter;

    // Properties
    gboolean enabled;

    // Internal state
    guint frame_count;
};

struct _GstAmpOsdClass {
    GstVideoFilterClass parent_class;
};

GType gst_amp_osd_get_type(void);

G_END_DECLS

GST_DEBUG_CATEGORY_STATIC(gst_amp_osd_debug);
#define GST_CAT_DEFAULT gst_amp_osd_debug

// Default values
#define DEFAULT_ENABLED TRUE

// Property IDs
enum { PROP_0, PROP_ENABLED };

// Function prototypes
static void
gst_amp_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void
gst_amp_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_amp_osd_finalize(GObject *object);
static GstFlowReturn gst_amp_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame);
static gboolean gst_amp_osd_start(GstBaseTransform *trans);
static gboolean gst_amp_osd_stop(GstBaseTransform *trans);

#define gst_amp_osd_parent_class parent_class
G_DEFINE_TYPE(GstAmpOsd, gst_amp_osd, GST_TYPE_VIDEO_FILTER);

static void gst_amp_osd_class_init(GstAmpOsdClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *vfilter_class = GST_VIDEO_FILTER_CLASS(klass);
    GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_amp_osd_set_property;
    gobject_class->get_property = gst_amp_osd_get_property;
    gobject_class->finalize = gst_amp_osd_finalize;

    vfilter_class->transform_frame_ip = gst_amp_osd_transform_frame_ip;
    trans_class->start = gst_amp_osd_start;
    trans_class->stop = gst_amp_osd_stop;

    // Install properties
    g_object_class_install_property(
        gobject_class,
        PROP_ENABLED,
        g_param_spec_boolean("enabled",
                             "Enabled",
                             "Enable or disable OSD overlay",
                             DEFAULT_ENABLED,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Set element metadata
    gst_element_class_set_static_metadata(element_class,
                                          "AMP OSD Overlay",
                                          "Filter/Video",
                                          "On-Screen Display overlay for ARGB video frames",
                                          "AMP Development Team");

    // Set pad templates
    GstCaps *caps = gst_caps_from_string("video/x-raw, format=(string){BGRA}");
    GstPadTemplate *src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    GstPadTemplate *sink_template =
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template(element_class, src_template);
    gst_element_class_add_pad_template(element_class, sink_template);
    gst_caps_unref(caps);
}

static void gst_amp_osd_init(GstAmpOsd *self) {
    // Initialize properties
    self->enabled = DEFAULT_ENABLED;
    self->frame_count = 0;

    GST_DEBUG_OBJECT(self, "Initialized AmpOsd element");
}

static void
gst_amp_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstAmpOsd *self = GST_AMP_OSD(object);

    switch (prop_id) {
    case PROP_ENABLED:
        self->enabled = g_value_get_boolean(value);
        GST_INFO_OBJECT(self, "Enabled set to: %d", self->enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_amp_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstAmpOsd *self = GST_AMP_OSD(object);

    switch (prop_id) {
    case PROP_ENABLED:
        g_value_set_boolean(value, self->enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_amp_osd_finalize(GObject *object) {
    GstAmpOsd *self = GST_AMP_OSD(object);

    GST_DEBUG_OBJECT(self, "Finalizing AmpOsd element");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_amp_osd_start(GstBaseTransform *trans) {
    GstAmpOsd *self = GST_AMP_OSD(trans);

    GST_INFO_OBJECT(self, "Starting AmpOsd element");
    self->frame_count = 0;

    return TRUE;
}

static gboolean gst_amp_osd_stop(GstBaseTransform *trans) {
    GstAmpOsd *self = GST_AMP_OSD(trans);

    GST_INFO_OBJECT(self, "Stopping AmpOsd element - processed %u frames", self->frame_count);

    return TRUE;
}

static std::unique_ptr<Osd::Layer>
draw_perf_layer(GstAmpOsd *self,
                float imgWidth,
                float imgHeight,
                const amp::PerceptionContext &perceptionContext) {
    constexpr float line_height = 16.0f;
    constexpr float x_offset = 10.0f;
    constexpr float y_offset = 10.0f;

    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    float line_y_offset = y_offset;
    for (const auto &line : perceptionContext.perfdata) {
        Osd::Text::draw(*layer,
                        Osd::Coordinate(x_offset, line_y_offset),
                        line,
                        Osd::Color("#11ff00ff"),
                        Osd::Color("#000000ff"),
                        "monospace",
                        line_height);
        line_y_offset += line_height; // Increment y_offset for next line
    }
    return layer;

    // moved here from ampperformance
    //  // Create temporary surface for font measurement
    //  cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    //  cairo_t *temp_cr = cairo_create(temp_surface);
    //  cairo_select_font_face(temp_cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
    //  CAIRO_FONT_WEIGHT_BOLD); cairo_set_font_size(temp_cr, self->font_size);

    // // Calculate dimensions - use sample text matching the actual format
    // // This should accommodate the longest metric names with proper column alignment
    // cairo_text_extents_t extents;
    // cairo_text_extents(temp_cr, "ultraface_postprocess  :  999.99ms  (p95:  999.99ms)",
    // &extents); double measured_width = extents.width;

    // // Calculate actual maximum line width from content
    // double max_content_width = measured_width;
    // for (const auto &line : lines) {
    //     cairo_text_extents(temp_cr, line.c_str(), &extents);
    //     if (extents.width > max_content_width) {
    //         max_content_width = extents.width;
    //     }
    // }

    // double line_height = self->font_size * 1.5;
    // double box_width = max_content_width + 40; // Extra padding for table-like appearance
    // double box_height = lines.size() * line_height + 20;

    // cairo_destroy(temp_cr);
    // cairo_surface_destroy(temp_surface);

    // // Calculate dimensions - width is stable due to fixed formatting, track max height only
    // guint cache_w = (guint)(box_width + 4);
    // guint desired_h = (guint)(box_height + 4);

    // // Track maximum height to prevent vertical flickering when metric count changes
    // if (desired_h > self->max_height) {
    //     self->max_height = desired_h;
    // }
    // guint cache_h = self->max_height;

    // if (self->overlay_cache) {
    //     cairo_surface_destroy(self->overlay_cache);
    // }

    // self->overlay_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cache_w, cache_h);
    // self->cache_width = cache_w;
    // self->cache_height = cache_h;

    // cairo_t *cr = cairo_create(self->overlay_cache);

    // // Clear with transparency
    // cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    // cairo_paint(cr);
    // cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // // Draw semi-transparent background box (use max dimensions for stable size)
    // double display_width = cache_w - 4;
    // double display_height = cache_h - 4;

    // cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, self->alpha);
    // cairo_rectangle(cr, 2, 2, display_width, display_height);
    // cairo_fill(cr);

    // // Draw border
    // cairo_set_source_rgba(cr, text_r, text_g, text_b, self->alpha);
    // cairo_set_line_width(cr, 2.0);
    // cairo_rectangle(cr, 2, 2, display_width, display_height);
    // cairo_stroke(cr);

    // // Draw text
    // cairo_set_source_rgb(cr, text_r, text_g, text_b);
    // cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    // cairo_set_font_size(cr, self->font_size);

    // double y_pos = 22;
    // for (const auto &line : lines) {
    //     cairo_move_to(cr, 12, y_pos);
    //     cairo_show_text(cr, line.c_str());
    //     y_pos += line_height;
    // }

    // cairo_destroy(cr);
    // self->cache_dirty = false;
}

static std::unique_ptr<Osd::Layer>
draw_segmentation_layer(GstAmpOsd *self, float imgWidth, float imgHeight, const amp::Map8 &segMap) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    // Get direct access to the layer's pixel data
    cairo_surface_flush(layer->surface);
    unsigned char *data = cairo_image_surface_get_data(layer->surface);
    int stride = cairo_image_surface_get_stride(layer->surface);

    // Calculate scale factors if segmentation map size differs from video frame
    auto scale_x = static_cast<float>(imgWidth) / static_cast<float>(segMap.width);
    auto scale_y = static_cast<float>(imgHeight) / static_cast<float>(segMap.height);

    // First pass: find min/max values in the segmentation map
    uint8_t min_value = 255U;
    uint8_t max_value = 0U;
    for (size_t i = 0; i < segMap.map.size(); ++i) {
        auto val = segMap.map[i];
        min_value = std::min(min_value, val);
        max_value = std::max(max_value, val);
    }

    // Avoid division by zero if map is uniform
    if (max_value == min_value) {
        return layer; // Return empty layer if no variation
    }

    // Second pass: draw with normalized values
    for (size_t y = 0; y < static_cast<size_t>(imgHeight); ++y) {
        for (size_t x = 0; x < static_cast<size_t>(imgWidth); ++x) {
            // Map frame coordinates to segmentation map coordinates
            auto seg_x = static_cast<size_t>(x / scale_x);
            auto seg_y = static_cast<size_t>(y / scale_y);

            // Bounds check
            if (seg_x >= segMap.width || seg_y >= segMap.height) {
                continue;
            }

            // Get value from segmentation map (0-255)
            auto value = segMap.map[seg_y * segMap.width + seg_x];

            // Min-max normalization: map [min_value, max_value] → [0, 255]
            auto range = static_cast<float>(max_value - min_value);
            auto normalized = static_cast<float>(value - min_value) / range;

            // Apply gamma correction for perceptual uniformity
            auto gamma = 2.2f;
            auto alpha = static_cast<uint8_t>(std::pow(normalized, gamma) * 255.0f);

            // Skip fully transparent pixels
            if (alpha == 0) {
                continue;
            }

            // Use bright cyan overlay for detected regions
            auto *pixel = data + y * stride + x * 4;
            pixel[0] = 255; // Blue
            pixel[1] = 255; // Green
            pixel[2] = 0;   // Red (BGR = cyan)
            pixel[3] = alpha;
        }
    }

    cairo_surface_mark_dirty(layer->surface);
    return layer;
}

static std::unique_ptr<Osd::Layer>
draw_detection_layer(GstAmpOsd *self,
                     float imgWidth,
                     float imgHeight,
                     const amp::PerceptionContext &perceptionContext) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    for (const auto &inferDetections : perceptionContext.rawDetections) {
        if (inferDetections.modelFamily == "yolo-obj") {
            for (const auto &box : inferDetections.rects) {
                Osd::ObjectBox::draw(*layer, box, Osd::Color("#ff0000ff"), 2.0f);
            }
        }
        if (inferDetections.modelFamily == "ultraface") {
            for (const auto &box : inferDetections.rects) {
                Osd::Circle::draw(*layer,
                                  Osd::Coordinate{box.x + box.w / 2.0f, box.y + box.h / 2.0f},
                                  box.w / 2.0f,
                                  Osd::Color("#2600ffff"),
                                  2.0f);
            }
            for (const auto &box : inferDetections.points) {
                Osd::Point::draw(
                    *layer, Osd::Coordinate{box.x, box.y}, Osd::Color("#ff00ffff"), 4.0f);
            }
        }
    }
    return layer;
}

static std::unique_ptr<Osd::Layer>
draw_debug_layer(GstAmpOsd *self, float imgWidth, float imgHeight) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);
    const auto centerCoord = Osd::CoordinateRel(0.5f, 0.5f, *layer);

    Osd::Point::draw(*layer, centerCoord, Osd::Color("#ff0000ff"), 10.0f);
    Osd::Text::draw(*layer,
                    centerCoord,
                    "center(" + std::to_string(centerCoord.x) + ", " +
                        std::to_string(centerCoord.y) + ")",
                    Osd::Color("#ffffffff"),
                    Osd::Color("#000000ff"),
                    "monospace",
                    16.0f);
    Osd::Text::draw(*layer,
                    Osd::CoordinateRel(0.f, 1.f, *layer) - Osd::Coordinate(0.f, 16.f),
                    "frame count= " + std::to_string(self->frame_count),
                    Osd::Color("#ffffffff"),
                    Osd::Color("#000000ff"),
                    "monospace",
                    16.0f);
    return layer;
}

static GstFlowReturn gst_amp_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame) {
    GstAmpOsd *self = GST_AMP_OSD(filter);

    if (!self->enabled) {
        return GST_FLOW_OK;
    }

    guint8 *imgData = (guint8 *)GST_VIDEO_FRAME_PLANE_DATA(frame, 0);
    float imgWidth = static_cast<float>(GST_VIDEO_FRAME_WIDTH(frame));
    float imgHeight = static_cast<float>(GST_VIDEO_FRAME_HEIGHT(frame));
    gint imgStride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);

    Osd::Layers_t layers;
    // Get PerceptionContextMeta
    if (const auto perceptionContextMeta = amp::PerceptionContextMeta::get(
            frame->buffer)) { // NOTE: PerceptionContextMeta locks internally!
        const auto perceptionContext = perceptionContextMeta->get_const_payload();
        if (perceptionContext) {
            // Draw segmentation maps from rawDetections (bottom layer)
            for (const auto &detection : perceptionContext->rawDetections) {
                for (const auto &segMap : detection.maps) {
                    if (!segMap.map.empty() && segMap.width > 0 && segMap.height > 0) {
                        layers.push_back(
                            draw_segmentation_layer(self, imgWidth, imgHeight, segMap));
                    }
                }
            }
            // Draw detection boxes on top of segmentation
            layers.push_back(draw_detection_layer(self, imgWidth, imgHeight, *perceptionContext));
            layers.push_back(draw_perf_layer(self, imgWidth, imgHeight, *perceptionContext));
        }
    }
    layers.push_back(draw_debug_layer(self, imgWidth, imgHeight));
    Osd::Canvas(imgData, imgWidth, imgHeight).paint(layers);

    self->frame_count++;
    return GST_FLOW_OK;
}

// Plugin initialization
static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_amp_osd_debug, "amposd", 0, "AMP OSD Overlay");

    return gst_element_register(plugin, "amposd", GST_RANK_NONE, GST_TYPE_AMP_OSD);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  amposd,
                  "AMP OSD Overlay - On-Screen Display for RGBA video frames",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
