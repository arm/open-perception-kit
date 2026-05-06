/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "amp/Bitmap.h"
#include "amp/Color.h"
#include "amp/Perception.h"
#include "gst/PerceptionMeta.h"
#include "gst/Tools.h"
#include "osd.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
    gchar *bgImagePath;

    // Internal state
    guint frameCount;
    std::optional<amp::Bitmap> bgImage;
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
#define DEFAULT_BG_IMAGE ""

// Property IDs
enum { PROP_0, PROP_ENABLED, PROP_BG_IMAGE };

// Function prototypes
static void
gst_amp_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void
gst_amp_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_amp_osd_finalize(GObject *object);
static GstFlowReturn gst_amp_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame);
static gboolean gst_amp_osd_start(GstBaseTransform *trans);
static gboolean gst_amp_osd_stop(GstBaseTransform *trans);
static bool gst_amp_osd_load_bg_image(GstAmpOsd *self);

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
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        PROP_BG_IMAGE,
        g_param_spec_string(
            "bg-image",
            "Background Image",
            "Optional replacement background image applied where the segmentation mask marks "
            "background",
            DEFAULT_BG_IMAGE,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Set element metadata
    gst_element_class_set_static_metadata(element_class,
                                          "AMP OSD Overlay",
                                          "Filter/Video",
                                          "On-Screen Display overlay for BGRA video frames",
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
    self->bgImagePath = g_strdup(DEFAULT_BG_IMAGE);
    self->frameCount = 0;
    self->bgImage.reset();

    GST_DEBUG_OBJECT(self, "Initialized AmpOsd element");
}

static bool gst_amp_osd_load_bg_image(GstAmpOsd *self) {
    self->bgImage.reset();

    if (self->bgImagePath == nullptr || self->bgImagePath[0] == '\0') {
        GST_DEBUG_OBJECT(self, "No bg-image configured");
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    constexpr int requestedChannels = STBI_rgb_alpha;
    constexpr size_t bytesPerPixel = 4U;

    stbi_uc *rawPixels =
        stbi_load(self->bgImagePath, &width, &height, &channels, requestedChannels);
    if (rawPixels == nullptr) {
        GST_WARNING_OBJECT(self,
                           "Failed to load bg-image '%s': %s",
                           self->bgImagePath,
                           stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                            : "unknown error");
        return false;
    }

    auto pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>(rawPixels, stbi_image_free);

    if (width <= 0 || height <= 0) {
        GST_WARNING_OBJECT(self, "Invalid bg-image '%s': invalid dimensions", self->bgImagePath);
        return false;
    }

    const auto imageWidth = static_cast<size_t>(width);
    const auto imageHeight = static_cast<size_t>(height);

    if (imageWidth > std::numeric_limits<size_t>::max() / imageHeight ||
        imageWidth * imageHeight > std::numeric_limits<size_t>::max() / bytesPerPixel) {
        GST_WARNING_OBJECT(self, "Invalid bg-image '%s': dimensions too large", self->bgImagePath);
        return false;
    }

    const size_t pixelCount = imageWidth * imageHeight;
    const size_t byteCount = pixelCount * bytesPerPixel;

    amp::Bitmap bitmap;
    bitmap.realloc(amp::Bitmap::Type::Uint32, imageWidth, imageHeight);

    std::span<const stbi_uc> srcPixels(rawPixels, byteCount);
    std::span<uint8_t> dstPixels(bitmap.getMutableData(), byteCount);

    for (size_t y = 0; y < imageHeight; ++y) {
        for (size_t x = 0; x < imageWidth; ++x) {
            const size_t outIdx = (y * imageWidth + x) * bytesPerPixel;

            // Convert RGBA from stb_image to BGRA expected by downstream code.
            dstPixels[outIdx + 0U] = srcPixels[outIdx + 2U]; // B
            dstPixels[outIdx + 1U] = srcPixels[outIdx + 1U]; // G
            dstPixels[outIdx + 2U] = srcPixels[outIdx + 0U]; // R
            dstPixels[outIdx + 3U] = srcPixels[outIdx + 3U]; // A
        }
    }

    self->bgImage = std::move(bitmap);

    GST_INFO_OBJECT(self, "Loaded bg-image '%s' (%dx%d)", self->bgImagePath, width, height);
    return true;
}

static void
gst_amp_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto *self = GST_AMP_OSD(object);

    switch (prop_id) {
    case PROP_ENABLED:
        self->enabled = g_value_get_boolean(value);
        GST_INFO_OBJECT(self, "Enabled set to: %d", self->enabled);
        break;

    case PROP_BG_IMAGE:
        g_free(self->bgImagePath);
        self->bgImagePath = g_value_dup_string(value);
        gst_amp_osd_load_bg_image(self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_amp_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto *self = GST_AMP_OSD(object);

    switch (prop_id) {
    case PROP_ENABLED:
        g_value_set_boolean(value, self->enabled);
        break;

    case PROP_BG_IMAGE:
        g_value_set_string(value, self->bgImagePath);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_amp_osd_finalize(GObject *object) {
    auto *self = GST_AMP_OSD(object);

    GST_DEBUG_OBJECT(self, "Finalizing AmpOsd element");

    g_free(self->bgImagePath);
    self->bgImagePath = nullptr;
    self->bgImage.reset();

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_amp_osd_start(GstBaseTransform *trans) {
    auto *self = GST_AMP_OSD(trans);

    GST_INFO_OBJECT(self, "Starting AmpOsd element");
    self->frameCount = 0;
    gst_amp_osd_load_bg_image(self);

    return TRUE;
}

static gboolean gst_amp_osd_stop(GstBaseTransform *trans) {
    auto *self = GST_AMP_OSD(trans);

    GST_INFO_OBJECT(self, "Stopping AmpOsd element - processed %u frames", self->frameCount);

    return TRUE;
}

static std::unique_ptr<Osd::Layer> drawPerformanceLayer([[maybe_unused]] GstAmpOsd *self,
                                                        float imgWidth,
                                                        float imgHeight,
                                                        const amp::Perception &perception) {
    constexpr float line_height = 16.0f;
    constexpr float x_offset = 10.0f;
    constexpr float y_offset = 10.0f;

    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    float line_y_offset = y_offset;
    for (const auto &line : perception.perfdata) {
        Osd::Text::draw(*layer,
                        Osd::Coordinate(x_offset, line_y_offset),
                        line,
                        amp::Colors::fromStringOrDefault("#66ff00ff"),
                        amp::Colors::fromStringOrDefault("#000000ff"),
                        "monospace",
                        line_height);
        line_y_offset += line_height;
    }

    return layer;
}

static std::unique_ptr<Osd::Layer> drawSegmentationLayer([[maybe_unused]] GstAmpOsd *self,
                                                         float imgWidth,
                                                         float imgHeight,
                                                         const amp::Bitmap &segMap) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    cairo_surface_flush(layer->surface);
    auto *data = cairo_image_surface_get_data(layer->surface);
    const int stride = cairo_image_surface_get_stride(layer->surface);

    const size_t sw = segMap.getWidth();
    const size_t sh = segMap.getHeight();

    if (sw == 0U || sh == 0U) {
        return layer;
    }

    const float scale_x = imgWidth / static_cast<float>(sw);
    const float scale_y = imgHeight / static_cast<float>(sh);

    const auto *seg = reinterpret_cast<const uint8_t *>(segMap.getData());

    // cairo ARGB32 memory is typically BGRA on little-endian systems.
    constexpr uint8_t B = 255U;
    constexpr uint8_t G = 255U;
    constexpr uint8_t R = 100U;
    constexpr float opacity = 1.0f;

    for (size_t y = 0; y < static_cast<size_t>(imgHeight); ++y) {
        const size_t sy = std::min(static_cast<size_t>(y / scale_y), sh - 1U);

        for (size_t x = 0; x < static_cast<size_t>(imgWidth); ++x) {
            const size_t sx = std::min(static_cast<size_t>(x / scale_x), sw - 1U);

            auto a = static_cast<uint8_t>(static_cast<float>(seg[sy * sw + sx]) * opacity);
            if (a == 0U) {
                continue;
            }

            auto *pixel = data + y * static_cast<size_t>(stride) + x * 4U;
            pixel[0] = B;
            pixel[1] = G;
            pixel[2] = R;
            pixel[3] = a;
        }
    }

    cairo_surface_mark_dirty(layer->surface);
    return layer;
}

static void replaceBackground(guint8 *imgData,
                              gint imgWidth,
                              gint imgHeight,
                              gint imgStride,
                              const amp::Bitmap &segMap,
                              const std::optional<amp::Bitmap> &bgImage) {
    assert(imgData != nullptr);

    const auto segWidth = segMap.getWidth();
    const auto segHeight = segMap.getHeight();

    if (segWidth == 0U || segHeight == 0U) {
        return;
    }

    const float segScaleX = static_cast<float>(imgWidth) / static_cast<float>(segWidth);
    const float segScaleY = static_cast<float>(imgHeight) / static_cast<float>(segHeight);

    const bool hasBgImage = bgImage.has_value();
    const float bgScaleX =
        hasBgImage ? (static_cast<float>(imgWidth) / static_cast<float>(bgImage->getWidth()))
                   : 1.0f;
    const float bgScaleY =
        hasBgImage ? (static_cast<float>(imgHeight) / static_cast<float>(bgImage->getHeight()))
                   : 1.0f;

    constexpr uint8_t threshold = 150U;

    for (gint y = 0; y < imgHeight; ++y) {
        auto *row = imgData + static_cast<size_t>(y) * static_cast<size_t>(imgStride);
        const auto segY = std::min(static_cast<size_t>(y / segScaleY), segHeight - 1U);

        for (gint x = 0; x < imgWidth; ++x) {
            const auto segX = std::min(static_cast<size_t>(x / segScaleX), segWidth - 1U);
            const auto maskValue = segMap.getData()[segY * segWidth + segX];

            // RVM mask values are inverted here: high values map to background.
            if (maskValue < threshold) {
                continue;
            }

            auto *pixel = row + static_cast<size_t>(x) * 4U;

            if (hasBgImage) {
                const auto bgX =
                    std::min(static_cast<size_t>(x / bgScaleX), bgImage->getWidth() - 1U);
                const auto bgY =
                    std::min(static_cast<size_t>(y / bgScaleY), bgImage->getHeight() - 1U);

                const auto *bgData = bgImage->getData();
                const size_t idx = (bgY * bgImage->getWidth() + bgX) * 4U;

                pixel[0] = bgData[idx + 0U];
                pixel[1] = bgData[idx + 1U];
                pixel[2] = bgData[idx + 2U];
            } else {
                // Cyan fallback when no replacement background image is configured.
                pixel[0] = 255U;
                pixel[1] = 255U;
                pixel[2] = 0U;
            }
        }
    }
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
    const float yaw = deg2rad(yawDeg);
    const float pitch = deg2rad(pitchDeg);

    float dx = -std::tan(yaw);
    float dy = -std::tan(pitch); // +pitch means up, but screen y grows down

    const float n = std::sqrt(dx * dx + dy * dy);
    if (n <= 0.0f) {
        outX = eyeX;
        outY = eyeY;
        return;
    }

    dx /= n;
    dy /= n;

    outX = eyeX + dx * lengthPx;
    outY = eyeY + dy * lengthPx;
}

static void drawGazeVectors(Osd::Layer *layer, const amp::Perception &perception) {
    amp::ConstPerceptionTools perceptionTools(perception);

    const auto yps =
        perceptionTools.getAllWithContentType<amp::Perception::YawPitch>("eyeYawPitch");

    for (const auto &yp : yps) {
        const auto parents = perceptionTools.getAllRectsWithContentType("humanFace", yp.parentUuid);

        if (parents.size() != 1U) {
            continue;
        }

        const auto &parent = parents[0];

        const float x = parent.x + parent.width / 2.0f;
        const float y = parent.y + parent.height / 2.0f;
        const float yaw = yp.yaw;
        const float pitch = yp.pitch;

        if (yaw < 0.1f && pitch < 0.1f && yaw > -0.1f && pitch > -0.1f) {
            continue;
        }

        float xEnd = x;
        float yEnd = y;
        gazeEndpoint(x, y, yaw, pitch, 120.0f, xEnd, yEnd);
        Osd::Arrow::draw(*layer, {x, y}, {xEnd, yEnd}, amp::Colors::lightGoldenrodYellow);
    }
}

static void drawCameraContactMarkers(Osd::Layer *layer, const amp::Perception &perception) {
    amp::ConstPerceptionTools perceptionTools(perception);

    for (const auto &inferLayer : perception.layers) {
        if (inferLayer.contentType != "cameraContact") {
            continue;
        }

        for (const auto &det : inferLayer.detections) {
            const auto *classification = std::get_if<amp::Perception::Classification>(&det);
            if (classification == nullptr || classification->candidates.empty()) {
                continue;
            }

            const auto &candidate = classification->candidates.front();
            if (candidate.classId < 0) {
                continue;
            }

            const auto parents =
                perceptionTools.getAllRectsWithContentType("humanFace", classification->parentUuid);

            if (parents.empty()) {
                continue;
            }

            const auto &face = parents.front();
            const float x = face.x + face.width * 0.5f;
            const float y = face.y + face.height * 0.5f;
            const bool hasCameraContact = candidate.classId == 1;
            const amp::Color markerColor = hasCameraContact ? amp::Colors::lime : amp::Colors::red;
            const float baseRadius = std::min(face.width, face.height) * 0.5f;
            const float markerRadius = hasCameraContact
                                           ? std::clamp(baseRadius * 0.65f, 18.0f, 80.0f)
                                           : std::clamp(baseRadius * 1.15f, 28.0f, 140.0f);
            const float markerThickness = hasCameraContact ? 5.0f : 8.0f;
            const float centerPointSize = hasCameraContact ? 10.0f : 14.0f;

            Osd::Circle::draw(
                *layer, Osd::Coordinate{x, y}, markerRadius, markerColor, markerThickness);
            Osd::Point::draw(*layer, Osd::Coordinate{x, y}, markerColor, centerPointSize);
        }
    }
}

static amp::Color colorForTrack(uint64_t trackId) {
    static const std::vector<amp::Color> palette = {
        amp::Colors::yellow,
        amp::Colors::lime,
        amp::Colors::cyan,
        amp::Colors::magenta,
        amp::Colors::orange,
        amp::Colors::deepSkyBlue,
        amp::Colors::fuchsia,
        amp::Colors::chartreuse,
    };

    return palette[trackId % palette.size()];
}

static amp::Color withAlpha(amp::Color color, float alpha) {
    const auto clamped = std::clamp(alpha, 0.0f, 1.0f);
    const auto a = static_cast<uint8_t>(clamped * 255.0f);
    return (color & 0x00ffffffu) | (static_cast<uint32_t>(a) << 24);
}

static void drawTrackTrace(Osd::Layer &layer, const amp::Perception::TrackTrace &trace) {
    if (trace.points.size() < 2U) {
        return;
    }

    const auto baseColor = colorForTrack(trace.trackId);
    const auto segmentCount = trace.points.size() - 1U;

    for (size_t i = 0; i < segmentCount; ++i) {
        const auto &from = trace.points[i];
        const auto &to = trace.points[i + 1U];

        const auto normalizedAge = static_cast<float>(i + 1U) / static_cast<float>(segmentCount);
        const auto alpha = 0.35f + (0.65f * normalizedAge);

        Osd::Arrow::draw(layer,
                         Osd::Coordinate{from.x, from.y},
                         Osd::Coordinate{to.x, to.y},
                         withAlpha(baseColor, alpha),
                         4.0f,
                         0.0f,
                         0.0f);
    }
}

static std::unique_ptr<Osd::Layer> drawPerceptionLayer([[maybe_unused]] GstAmpOsd *self,
                                                       float imgWidth,
                                                       float imgHeight,
                                                       const amp::Perception &perception) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    for (const auto &inferLayer : perception.layers) {
        if (inferLayer.contentType == "trackTrace") {
            for (const auto &det : inferLayer.detections) {
                const auto &trace = std::get<amp::Perception::TrackTrace>(det);
                drawTrackTrace(*layer, trace);
            }
            continue;
        }

        if (inferLayer.contentType == "genericObject") {
            for (const auto &det : inferLayer.detections) {
                const auto &box = std::get<amp::Perception::Rect>(det);
                Osd::ObjectBox::draw(
                    *layer, box, amp::Colors::fromStringOrDefault("#ff0000ff"), 2.0f);
            }
        }

        if (inferLayer.contentType == "personClassification") {
            for (const auto &det : inferLayer.detections) {
                const auto &pc = std::get<amp::Perception::PersonClassification>(det);

                const bool isPerson = pc.yesConfidence > pc.noConfidence;
                const std::string label = isPerson ? "PERSON" : "NON-PERSON";
                const auto color = isPerson ? amp::Colors::fromStringOrDefault("#66ff00ff")
                                            : amp::Colors::fromStringOrDefault("#ff4444ff");
                constexpr float fontSize = 64.0f;

                cairo_select_font_face(
                    layer->context, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(layer->context, fontSize);
                cairo_text_extents_t ex;
                cairo_text_extents(layer->context, label.c_str(), &ex);

                const float x = (imgWidth - ex.width) / 2.0f - ex.x_bearing;
                const float y = (imgHeight - ex.height) / 2.0f - ex.y_bearing;

                uint64_t timeMs = amp::TsUtcNs() / 1000000U;
                if (timeMs % 1000 < 800) {
                    Osd::Text::draw(*layer,
                                    Osd::Coordinate(x, y),
                                    label,
                                    color,
                                    amp::Colors::fromStringOrDefault("#000000cc"),
                                    "monospace",
                                    fontSize);
                }
            }
        }

        if (inferLayer.contentType == "humanFace") {
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

                const float fontSize = 14.0f;
                const float lineHeight = fontSize * 1.5f;
                const auto numResults = classification.candidates.size();

                // Calculate starting position (lower-left corner with padding)
                const float padding = 10.0f;
                const float startX = padding;
                const float startY =
                    imgHeight - (static_cast<float>(numResults) * lineHeight) - (2.0f * padding);

                for (size_t i = 0U; i < classification.candidates.size(); ++i) {
                    const auto &result = classification.candidates[i];

                    std::ostringstream oss;
                    oss << "#" << (i + 1U) << ": " << result.text << " (" << std::fixed
                        << std::setprecision(1) << (result.confidence * 100.0f) << "%)";

                    const float textX = startX;
                    const float textY = startY + static_cast<float>(i) * lineHeight;

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
    }

    drawGazeVectors(layer.get(), perception);
    drawCameraContactMarkers(layer.get(), perception);

    return layer;
}

static void gst_amp_osd_process_layer(GstAmpOsd *self,
                                      guint8 *imgData,
                                      gint imgStride,
                                      float imgWidth,
                                      float imgHeight,
                                      Osd::Layers_t &layers,
                                      const amp::Perception::Layer &layer) {
    if (layer.contentType == "segmentation") {
        const bool useBackgroundReplacement = layer.compositingMode == "backgroundReplacement";

        for (const auto &det : layer.detections) {
            const auto *sm = std::get_if<amp::Perception::SegmentationMap>(&det);
            if (sm == nullptr || sm->bitmap.empty() || sm->bitmap.getWidth() == 0U ||
                sm->bitmap.getHeight() == 0U) {
                continue;
            }

            if (useBackgroundReplacement) {
                replaceBackground(imgData,
                                  static_cast<gint>(imgWidth),
                                  static_cast<gint>(imgHeight),
                                  imgStride,
                                  sm->bitmap,
                                  self->bgImage);
            } else {
                layers.push_back(drawSegmentationLayer(self, imgWidth, imgHeight, sm->bitmap));
            }
        }
    }
}

static GstFlowReturn gst_amp_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame) {
    auto *self = GST_AMP_OSD(filter);

    if (!self->enabled) {
        return GST_FLOW_OK;
    }

    auto *imgData = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
    const float imgWidth = static_cast<float>(GST_VIDEO_FRAME_WIDTH(frame));
    const float imgHeight = static_cast<float>(GST_VIDEO_FRAME_HEIGHT(frame));
    const gint imgStride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);

    Osd::Layers_t layers;

    if (auto perception = amp::PerceptionMeta::read(frame->buffer); perception != nullptr) {
        for (const auto &layer : perception->layers) {
            gst_amp_osd_process_layer(self, imgData, imgStride, imgWidth, imgHeight, layers, layer);
        }

        layers.push_back(drawPerceptionLayer(self, imgWidth, imgHeight, *perception));
        layers.push_back(drawPerformanceLayer(self, imgWidth, imgHeight, *perception));

        Osd::Canvas(imgData, imgWidth, imgHeight).paint(layers);
    }

    ++self->frameCount;
    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_amp_osd_debug, "amposd", 0, "AMP OSD Overlay");

    return gst_element_register(plugin, "amposd", GST_RANK_NONE, GST_TYPE_AMP_OSD);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  amposd,
                  "AMP OSD Overlay - On-Screen Display for BGRA video frames",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
