/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "gst/PerceptionMeta.h"
#include "gst/Tools.h"
#include "osd.h"
#include "pek/Bitmap.h"
#include "pek/Color.h"
#include "pek/Perception.h"
#include "pek/Tools.h"

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

#ifndef PACKAGE
#define PACKAGE "pek-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_PEK_OSD (gst_pek_osd_get_type())
#define GST_PEK_OSD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PEK_OSD, GstPekOsd))
#define GST_PEK_OSD_CLASS(klass)                                                                   \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PEK_OSD, GstPekOsdClass))
#define GST_IS_PEK_OSD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PEK_OSD))
#define GST_IS_PEK_OSD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PEK_OSD))

typedef struct _GstPekOsd GstPekOsd;
typedef struct _GstPekOsdClass GstPekOsdClass;

struct _GstPekOsd {
    GstVideoFilter videofilter;

    // Properties
    gboolean enabled;
    gboolean enablePerfdata;
    gchar *bgImagePath;

    // Internal state
    guint frameCount;
    std::optional<pek::Bitmap> bgImage;
};

struct _GstPekOsdClass {
    GstVideoFilterClass parent_class;
};

GType gst_pek_osd_get_type(void);

G_END_DECLS

GST_DEBUG_CATEGORY_STATIC(gst_pek_osd_debug);
#define GST_CAT_DEFAULT gst_pek_osd_debug

// Default values
#define DEFAULT_ENABLED TRUE
#define DEFAULT_ENABLE_PERFDATA TRUE
#define DEFAULT_BG_IMAGE ""

// Property IDs
enum { PROP_0, PROP_ENABLED, PROP_ENABLE_PERFDATA, PROP_BG_IMAGE };

// Function prototypes
static void
gst_pek_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void
gst_pek_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_pek_osd_finalize(GObject *object);
static GstFlowReturn gst_pek_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame);
static gboolean gst_pek_osd_start(GstBaseTransform *trans);
static gboolean gst_pek_osd_stop(GstBaseTransform *trans);
static bool gst_pek_osd_load_bg_image(GstPekOsd *self);

#define gst_pek_osd_parent_class parent_class
G_DEFINE_TYPE(GstPekOsd, gst_pek_osd, GST_TYPE_VIDEO_FILTER);

static void gst_pek_osd_class_init(GstPekOsdClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *vfilter_class = GST_VIDEO_FILTER_CLASS(klass);
    GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_pek_osd_set_property;
    gobject_class->get_property = gst_pek_osd_get_property;
    gobject_class->finalize = gst_pek_osd_finalize;

    vfilter_class->transform_frame_ip = gst_pek_osd_transform_frame_ip;
    trans_class->start = gst_pek_osd_start;
    trans_class->stop = gst_pek_osd_stop;

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
        PROP_ENABLE_PERFDATA,
        g_param_spec_boolean("enable-perfdata",
                             "Enable Perfdata",
                             "Enable or disable rendering performance metrics from perception metadata",
                             DEFAULT_ENABLE_PERFDATA,
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
                                          "PEK OSD Overlay",
                                          "Filter/Video",
                                          "On-Screen Display overlay for BGRA video frames",
                                          "PEK Development Team");

    // Set pad templates
    GstCaps *caps = gst_caps_from_string("video/x-raw, format=(string){BGRA}");
    GstPadTemplate *src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    GstPadTemplate *sink_template =
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template(element_class, src_template);
    gst_element_class_add_pad_template(element_class, sink_template);
    gst_caps_unref(caps);
}

static void gst_pek_osd_init(GstPekOsd *self) {
    // Initialize properties
    self->enabled = DEFAULT_ENABLED;
    self->enablePerfdata = DEFAULT_ENABLE_PERFDATA;
    self->bgImagePath = g_strdup(DEFAULT_BG_IMAGE);
    self->frameCount = 0;
    self->bgImage.reset();

    GST_DEBUG_OBJECT(self, "Initialized PekOsd element");
}

static bool gst_pek_osd_load_bg_image(GstPekOsd *self) {
    self->bgImage.reset();

    if (self->bgImagePath == nullptr || self->bgImagePath[0] == '\0') {
        GST_DEBUG_OBJECT(self, "No bg-image configured");
        return false;
    }

    size_t imageWidth = 0;
    size_t imageHeight = 0;
    auto loadedPixels = pek::Tools::loadImageFile(self->bgImagePath, imageWidth, imageHeight);
    if (!loadedPixels) {
        GST_WARNING_OBJECT(self,
                           "Failed to load bg-image '%s': %s",
                           self->bgImagePath,
                           loadedPixels.error().toString().c_str());
        return false;
    }

    constexpr size_t srcBytesPerPixel = 3U;
    constexpr size_t dstBytesPerPixel = 4U;

    if (imageWidth > std::numeric_limits<size_t>::max() / imageHeight ||
        imageWidth * imageHeight > std::numeric_limits<size_t>::max() / dstBytesPerPixel) {
        GST_WARNING_OBJECT(self, "Invalid bg-image '%s': dimensions too large", self->bgImagePath);
        return false;
    }

    const size_t pixelCount = imageWidth * imageHeight;
    const size_t srcByteCount = pixelCount * srcBytesPerPixel;
    const size_t dstByteCount = pixelCount * dstBytesPerPixel;
    const auto pixels = std::move(*loadedPixels);

    if (pixels.size() != srcByteCount) {
        GST_WARNING_OBJECT(
            self, "Invalid bg-image '%s': decoded byte count mismatch", self->bgImagePath);
        return false;
    }

    pek::Bitmap bitmap;
    bitmap.realloc(pek::Bitmap::Type::Uint32, imageWidth, imageHeight);

    std::span<const uint8_t> srcPixels(pixels.data(), pixels.size());
    std::span<uint8_t> dstPixels(bitmap.getMutableData(), dstByteCount);

    for (size_t y = 0; y < imageHeight; ++y) {
        for (size_t x = 0; x < imageWidth; ++x) {
            const size_t srcIdx = (y * imageWidth + x) * srcBytesPerPixel;
            const size_t dstIdx = (y * imageWidth + x) * dstBytesPerPixel;

            // Convert RGB from Tools::loadImageFile to BGRA expected by downstream code.
            dstPixels[dstIdx + 0U] = srcPixels[srcIdx + 2U]; // B
            dstPixels[dstIdx + 1U] = srcPixels[srcIdx + 1U]; // G
            dstPixels[dstIdx + 2U] = srcPixels[srcIdx + 0U]; // R
            dstPixels[dstIdx + 3U] = 255U;                   // A
        }
    }

    self->bgImage = std::move(bitmap);

    GST_INFO_OBJECT(
        self, "Loaded bg-image '%s' (%zux%zu)", self->bgImagePath, imageWidth, imageHeight);
    return true;
}

static void
gst_pek_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto *self = GST_PEK_OSD(object);

    switch (prop_id) {
    case PROP_ENABLED:
        self->enabled = g_value_get_boolean(value);
        GST_INFO_OBJECT(self, "Enabled set to: %d", self->enabled);
        break;

    case PROP_ENABLE_PERFDATA:
        self->enablePerfdata = g_value_get_boolean(value);
        GST_INFO_OBJECT(self, "Enable perfdata set to: %d", self->enablePerfdata);
        break;

    case PROP_BG_IMAGE:
        g_free(self->bgImagePath);
        self->bgImagePath = g_value_dup_string(value);
        gst_pek_osd_load_bg_image(self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_pek_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto *self = GST_PEK_OSD(object);

    switch (prop_id) {
    case PROP_ENABLED:
        g_value_set_boolean(value, self->enabled);
        break;

    case PROP_ENABLE_PERFDATA:
        g_value_set_boolean(value, self->enablePerfdata);
        break;

    case PROP_BG_IMAGE:
        g_value_set_string(value, self->bgImagePath);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_pek_osd_finalize(GObject *object) {
    auto *self = GST_PEK_OSD(object);

    GST_DEBUG_OBJECT(self, "Finalizing PekOsd element");

    g_free(self->bgImagePath);
    self->bgImagePath = nullptr;
    self->bgImage.reset();

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_pek_osd_start(GstBaseTransform *trans) {
    auto *self = GST_PEK_OSD(trans);

    GST_INFO_OBJECT(self, "Starting PekOsd element");
    self->frameCount = 0;
    gst_pek_osd_load_bg_image(self);

    return TRUE;
}

static gboolean gst_pek_osd_stop(GstBaseTransform *trans) {
    auto *self = GST_PEK_OSD(trans);

    GST_INFO_OBJECT(self, "Stopping PekOsd element - processed %u frames", self->frameCount);

    return TRUE;
}

static std::unique_ptr<pek::osd::Layer> drawPerformanceLayer([[maybe_unused]] GstPekOsd *self,
                                                             float imgWidth,
                                                             float imgHeight,
                                                             const pek::Perception &perception) {
    constexpr float line_height = 16.0f;
    constexpr float x_offset = 10.0f;
    constexpr float y_offset = 10.0f;

    auto layer = std::make_unique<pek::osd::Layer>(imgWidth, imgHeight);

    float line_y_offset = y_offset;
    for (const auto &line : perception.perfdata) {
        pek::osd::Text::draw(*layer,
                             pek::osd::Coordinate(x_offset, line_y_offset),
                             line,
                             pek::Colors::fromStringOrDefault("#66ff00ff"),
                             pek::Colors::fromStringOrDefault("#000000ff"),
                             "monospace",
                             line_height);
        line_y_offset += line_height;
    }

    return layer;
}

static std::unique_ptr<pek::osd::Layer> drawSegmentationLayer([[maybe_unused]] GstPekOsd *self,
                                                              float imgWidth,
                                                              float imgHeight,
                                                              const pek::Bitmap &segMap) {
    auto layer = std::make_unique<pek::osd::Layer>(imgWidth, imgHeight);

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
                              const pek::Bitmap &segMap,
                              const std::optional<pek::Bitmap> &bgImage) {
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

static void drawGazeVectors(pek::osd::Layer *layer, const pek::Perception &perception) {
    pek::ConstPerceptionTools perceptionTools(perception);

    const auto yps =
        perceptionTools.getAllWithContentType<pek::Perception::YawPitch>("eyeYawPitch");

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
        pek::osd::Arrow::draw(*layer, {x, y}, {xEnd, yEnd}, pek::Colors::lightGoldenrodYellow);
    }
}

static void drawCameraContactMarkers(pek::osd::Layer *layer, const pek::Perception &perception) {
    pek::ConstPerceptionTools perceptionTools(perception);

    for (const auto &inferLayer : perception.layers) {
        if (inferLayer.contentType != "cameraContact") {
            continue;
        }

        for (const auto &det : inferLayer.detections) {
            const auto *classification = std::get_if<pek::Perception::Classification>(&det);
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
            const pek::Color markerColor = hasCameraContact ? pek::Colors::lime : pek::Colors::red;
            const float baseRadius = std::min(face.width, face.height) * 0.5f;
            const float markerRadius = hasCameraContact
                                           ? std::clamp(baseRadius * 0.65f, 18.0f, 80.0f)
                                           : std::clamp(baseRadius * 1.15f, 28.0f, 140.0f);
            const float markerThickness = hasCameraContact ? 5.0f : 8.0f;
            const float centerPointSize = hasCameraContact ? 10.0f : 14.0f;

            pek::osd::Circle::draw(
                *layer, pek::osd::Coordinate{x, y}, markerRadius, markerColor, markerThickness);
            pek::osd::Point::draw(*layer, pek::osd::Coordinate{x, y}, markerColor, centerPointSize);
        }
    }
}

static pek::Color colorForTrack(uint64_t trackId) {
    static const std::vector<pek::Color> palette = {
        pek::Colors::yellow,
        pek::Colors::lime,
        pek::Colors::cyan,
        pek::Colors::magenta,
        pek::Colors::orange,
        pek::Colors::deepSkyBlue,
        pek::Colors::fuchsia,
        pek::Colors::chartreuse,
    };

    return palette[trackId % palette.size()];
}

static pek::Color withAlpha(pek::Color color, float alpha) {
    const auto clamped = std::clamp(alpha, 0.0f, 1.0f);
    const auto a = static_cast<uint8_t>(clamped * 255.0f);
    return (color & 0x00ffffffu) | (static_cast<uint32_t>(a) << 24);
}

static void drawTrackTrace(pek::osd::Layer &layer, const pek::Perception::TrackTrace &trace) {
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

        pek::osd::Arrow::draw(layer,
                              pek::osd::Coordinate{from.x, from.y},
                              pek::osd::Coordinate{to.x, to.y},
                              withAlpha(baseColor, alpha),
                              4.0f,
                              0.0f,
                              0.0f);
    }
}

static std::unique_ptr<pek::osd::Layer> drawPerceptionLayer([[maybe_unused]] GstPekOsd *self,
                                                            float imgWidth,
                                                            float imgHeight,
                                                            const pek::Perception &perception) {
    auto layer = std::make_unique<pek::osd::Layer>(imgWidth, imgHeight);

    for (const auto &inferLayer : perception.layers) {
        if (inferLayer.contentType == "trackTrace") {
            for (const auto &det : inferLayer.detections) {
                const auto &trace = std::get<pek::Perception::TrackTrace>(det);
                drawTrackTrace(*layer, trace);
            }
            continue;
        }

        if (inferLayer.contentType == "genericObject") {
            for (const auto &det : inferLayer.detections) {
                const auto &box = std::get<pek::Perception::Rect>(det);
                pek::osd::ObjectBox::draw(
                    *layer, box, pek::Colors::fromStringOrDefault("#ff0000ff"), 2.0f);
            }
        }

        if (inferLayer.contentType == "personClassification") {
            for (const auto &det : inferLayer.detections) {
                const auto &pc = std::get<pek::Perception::PersonClassification>(det);

                const bool isPerson = pc.yesConfidence > pc.noConfidence;
                const std::string label = isPerson ? "PERSON" : "NON-PERSON";
                const auto color = isPerson ? pek::Colors::fromStringOrDefault("#66ff00ff")
                                            : pek::Colors::fromStringOrDefault("#ff4444ff");
                constexpr float fontSize = 64.0f;

                cairo_select_font_face(
                    layer->context, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(layer->context, fontSize);
                cairo_text_extents_t ex;
                cairo_text_extents(layer->context, label.c_str(), &ex);

                const float x = (imgWidth - ex.width) / 2.0f - ex.x_bearing;
                const float y = (imgHeight - ex.height) / 2.0f - ex.y_bearing;

                uint64_t timeMs = pek::Time::utcMs();
                if (timeMs % 1000 < 800) {
                    pek::osd::Text::draw(*layer,
                                         pek::osd::Coordinate(x, y),
                                         label,
                                         color,
                                         pek::Colors::fromStringOrDefault("#000000cc"),
                                         "monospace",
                                         fontSize);
                }
            }
        }

        if (inferLayer.contentType == "humanFace") {
            for (const auto &det : inferLayer.detections) {
                const auto &box = std::get<pek::Perception::Rect>(det);
                pek::osd::Circle::draw(
                    *layer,
                    pek::osd::Coordinate{box.x + box.width / 2.0f, box.y + box.height / 2.0f},
                    box.width / 2.0f,
                    pek::Colors::fromStringOrDefault("#2600ffff"),
                    2.0f);
            }
        }

        if (inferLayer.contentType == "classification") {
            for (const auto &det : inferLayer.detections) {
                // Draw classification results as a label list in lower-left corner
                const auto &classification = std::get<pek::Perception::Classification>(det);

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

                    pek::osd::Text::draw(*layer,
                                         pek::osd::Coordinate(textX, textY),
                                         oss.str(),
                                         pek::Colors::fromStringOrDefault("#ffffffff"),
                                         pek::Colors::fromStringOrDefault("#000000ff"),
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

static void gst_pek_osd_process_layer(GstPekOsd *self,
                                      guint8 *imgData,
                                      gint imgStride,
                                      float imgWidth,
                                      float imgHeight,
                                      pek::osd::Layers_t &layers,
                                      const pek::Perception::Layer &layer) {
    if (layer.contentType == "segmentation") {
        const bool useBackgroundReplacement = layer.compositingMode == "backgroundReplacement";

        for (const auto &det : layer.detections) {
            const auto *sm = std::get_if<pek::Perception::SegmentationMap>(&det);
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

static GstFlowReturn gst_pek_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame) {
    auto *self = GST_PEK_OSD(filter);

    if (!self->enabled) {
        return GST_FLOW_OK;
    }

    auto *imgData = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
    const float imgWidth = static_cast<float>(GST_VIDEO_FRAME_WIDTH(frame));
    const float imgHeight = static_cast<float>(GST_VIDEO_FRAME_HEIGHT(frame));
    const gint imgStride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);

    pek::osd::Layers_t layers;

    if (auto perception = pek::PerceptionMeta::read(frame->buffer); perception != nullptr) {
        for (const auto &layer : perception->layers) {
            gst_pek_osd_process_layer(self, imgData, imgStride, imgWidth, imgHeight, layers, layer);
        }

        layers.push_back(drawPerceptionLayer(self, imgWidth, imgHeight, *perception));
        if (self->enablePerfdata) {
            layers.push_back(drawPerformanceLayer(self, imgWidth, imgHeight, *perception));
        }

        pek::osd::Canvas(imgData, imgWidth, imgHeight).paint(layers);
    }

    ++self->frameCount;
    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_pek_osd_debug, "pekosd", 0, "PEK OSD Overlay");

    return gst_element_register(plugin, "pekosd", GST_RANK_NONE, GST_TYPE_PEK_OSD);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  pekosd,
                  "PEK OSD Overlay - On-Screen Display for BGRA video frames",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
