#include "amp/Bitmap.h"
#include "amp/Color.h"
#include "amp/Perception.h"
#include "gst/PerceptionContextMeta.h"
#include "osd.h"
#include <cmath>
#include <cstring>
#include <fmt/core.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
#include <iomanip>
#include <new>
#include <sstream>

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
    guint frameCount;
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
    self->frameCount = 0;

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
    self->frameCount = 0;

    return TRUE;
}

static gboolean gst_amp_osd_stop(GstBaseTransform *trans) {
    GstAmpOsd *self = GST_AMP_OSD(trans);

    GST_INFO_OBJECT(self, "Stopping AmpOsd element - processed %u frames", self->frameCount);

    return TRUE;
}

static std::unique_ptr<Osd::Layer> drawPerformanceLayer(GstAmpOsd *self,
                                                        float imgWidth,
                                                        float imgHeight,
                                                        const amp::Perception &perceptionContext) {
    constexpr float line_height = 16.0f;
    constexpr float x_offset = 10.0f;
    constexpr float y_offset = 10.0f;

    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    float line_y_offset = y_offset;
    for (const auto &line : perceptionContext.perfdata) {
        Osd::Text::draw(*layer,
                        Osd::Coordinate(x_offset, line_y_offset),
                        line,
                        amp::Colors::fromStringOrDefault("#66ff00ff"),
                        amp::Colors::fromStringOrDefault("#000000ff"),
                        "monospace",
                        line_height);
        line_y_offset += line_height; // Increment y_offset for next line
    }
    return layer;
}

static std::unique_ptr<Osd::Layer>
drawSegmentationLayer(GstAmpOsd *self, float imgWidth, float imgHeight, const amp::Bitmap &segMap) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    // Get direct access to the layer's pixel data
    cairo_surface_flush(layer->surface);
    unsigned char *data = cairo_image_surface_get_data(layer->surface);
    int stride = cairo_image_surface_get_stride(layer->surface);

    // Calculate scale factors if segmentation map size differs from video frame
    auto scale_x = static_cast<float>(imgWidth) / static_cast<float>(segMap.getWidth());
    auto scale_y = static_cast<float>(imgHeight) / static_cast<float>(segMap.getHeight());

    // First pass: find min/max values in the segmentation map
    uint8_t min_value = 255U;
    uint8_t max_value = 0U;
    for (size_t i = 0; i < segMap.getWidth() * segMap.getHeight(); ++i) {
        auto val = segMap.getData()[i];
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
            if (seg_x >= segMap.getWidth() || seg_y >= segMap.getHeight()) {
                continue;
            }

            // Get value from segmentation map (0-255)
            auto value = segMap.getData()[seg_y * segMap.getWidth() + seg_x];

            // Min-max normalization: map [min_value, max_value] → [0, 255]
            auto range = static_cast<float>(max_value - min_value);
            auto normalized = static_cast<float>(value - min_value) / range;

            auto alpha = static_cast<uint8_t>(normalized * 255.0f);

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

static inline float deg2rad(float d) {
    return d * 3.1415926535f / 180.0f;
}

static inline void gazeEndpoint(float eyeX,
                                float eyeY,
                                float yawDeg,
                                float pitchDeg,
                                float lengthPx,
                                float &outX,
                                float &outY) {
    float yaw = deg2rad(yawDeg);
    float pitch = deg2rad(pitchDeg);

    float dx = -std::tan(yaw);
    float dy = -std::tan(pitch); // minus because +pitch means up, but screen y goes down

    float n = std::sqrt(dx * dx + dy * dy);

    dx /= n;
    dy /= n;

    outX = eyeX + dx * lengthPx;
    outY = eyeY + dy * lengthPx;
}

static void drawGazeVectors(Osd::Layer *layer, const amp::Perception &perception) {
    amp::ConstPerceptionTools perceptionTools(perception);

    std::vector<amp::Perception::YawPitch> yps =
        perceptionTools.getAllWithContentType<amp::Perception::YawPitch>("eye-yp");

    for (const auto &yp : yps) {
        std::vector<amp::Perception::Rect> parents =
            perceptionTools.getAllRectsWithContentType("human-face", yp.parentUuid);

        assert(parents.size() == 1);

        amp::Perception::Rect parent = parents[0];

        float x = parent.x + parent.width / 2;
        float y = parent.y + parent.height / 2;
        float yaw = yp.yaw;
        float pitch = yp.pitch;

        // if(yp.confidence < 0.1f) continue;

        if (yaw < 0.1f && pitch < 0.1f && yaw > -0.1f && pitch > -0.1f)
            continue;

        float xEnd, yEnd;
        gazeEndpoint(x, y, yaw, pitch, 120, xEnd, yEnd);
        Osd::Arrow::draw(*layer, {x, y}, {xEnd, yEnd}, amp::Colors::lightGoldenrodYellow);
        // Osd::Point::draw(*layer, Osd::Coordinate{x, y},
        // amp::Colors::fromStringOrDefault("#ff0000ff"), 10.0f);

        // Osd::Point::draw(*layer, Osd::Coordinate{xEnd, yEnd},
        // amp::Colors::fromStringOrDefault("#00ff00ff"), 15.0f);
    }
}

static std::unique_ptr<Osd::Layer> drawPerceptionLayer(GstAmpOsd *self,
                                                       float imgWidth,
                                                       float imgHeight,
                                                       const amp::Perception &perception) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    for (const auto &inferLayer : perception.layers) {
        if (inferLayer.contentType == "generic-object") {
            for (const auto &det : inferLayer.detections) {
                const auto &box = std::get<amp::Perception::Rect>(det);
                Osd::ObjectBox::draw(
                    *layer, box, amp::Colors::fromStringOrDefault("#ff0000ff"), 2.0f);
            }
        }
        if (inferLayer.contentType == "human-face") {
            for (const auto &det : inferLayer.detections) {
                const auto &box = std::get<amp::Perception::Rect>(det);
                Osd::Circle::draw(
                    *layer,
                    Osd::Coordinate{box.x + box.width / 2.0f, box.y + box.height / 2.0f},
                    box.width / 2.0f,
                    amp::Colors::fromStringOrDefault("#2600ffff"),
                    2.0f);
            }
        }

        if (inferLayer.contentType == "classification") {
            for (const auto &det : inferLayer.detections) {
                // Draw classification results as a label list in lower-left corner
                const auto &classification = std::get<amp::Perception::Classification>(det);

                const auto fontSize = 14.0f;
                const auto lineHeight = fontSize * 1.5f;
                const auto numResults = classification.candidates.size();

                // Calculate starting position (lower-left corner with padding)
                const auto padding = 10.0f;
                const auto startX = padding;
                const auto startY = imgHeight - (numResults * lineHeight) - (2.0f * padding);

                for (auto i = 0U; i < classification.candidates.size(); ++i) {
                    const auto &result = classification.candidates[i];

                    std::ostringstream oss;
                    oss << "#" << (i + 1U) << ": " << result.text << " (" << std::fixed
                        << std::setprecision(1) << (result.confidence * 100.0f) << "%)";

                    float textX = startX;
                    float textY = startY + i * lineHeight;

                    Osd::Text::draw(*layer,
                                    Osd::Coordinate(textX, textY),
                                    oss.str(),
                                    amp::Colors::fromStringOrDefault("#ffffffff"),
                                    amp::Colors::fromStringOrDefault("#000000ff"),
                                    "monospace",
                                    fontSize);
                }
            }
        }

        drawGazeVectors(layer.get(), perception);
    }

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

    // "ocr-detection-segmentation"
    Osd::Layers_t layers;
    // Get PerceptionContextMeta
    if (const auto perceptionContextMeta = amp::PerceptionContextMeta::get(frame->buffer)) {
        const auto perceptionContext = perceptionContextMeta->get_const_payload();
        if (perceptionContext) {
            for (const auto &layer : perceptionContext->layers) {
                if (layer.contentType == "ocr-detection-segmentation") {
                    for (const auto &det : layer.detections) {
                        const auto &sm = std::get<amp::Perception::SegmentationMap>(det);
                        if (!sm.bitmap.empty() && sm.bitmap.getWidth() > 0 &&
                            sm.bitmap.getHeight() > 0) {
                            layers.push_back(
                                drawSegmentationLayer(self, imgWidth, imgHeight, sm.bitmap));
                        }
                    }
                }
            }
            // Draw detection boxes on top of segmentation
            layers.push_back(drawPerceptionLayer(self, imgWidth, imgHeight, *perceptionContext));
            layers.push_back(drawPerformanceLayer(self, imgWidth, imgHeight, *perceptionContext));
        }
    }

    Osd::Canvas(imgData, imgWidth, imgHeight).paint(layers);

    self->frameCount++;
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
