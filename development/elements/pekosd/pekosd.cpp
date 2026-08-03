/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "gst/FrameResultsMeta.h"
#include "gst/Tools.h"
#include "osd.h"
#include "pek/Bitmap.h"
#include "pek/Color.h"
#include "pek/FrameResults.h"
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
#include <set>
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
    gboolean performanceOverlayEnabled;
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

namespace Osd = pek::osd;

// Default values
#define DEFAULT_ENABLED FALSE
#define DEFAULT_PERFORMANCE_OVERLAY_ENABLED TRUE
#define DEFAULT_BG_IMAGE ""

// Property IDs
enum class PropertyId : guint {
    Reserved = 0,
    Enabled,
    PerformanceOverlayEnabled,
    BgImage,
};

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
        static_cast<guint>(PropertyId::Enabled),
        g_param_spec_boolean("enabled",
                             "Enabled",
                             "Enable or disable OSD overlay",
                             DEFAULT_ENABLED,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(PropertyId::PerformanceOverlayEnabled),
        g_param_spec_boolean("performance-overlay-enabled",
                             "Performance Overlay Enabled",
                             "Enable or disable drawing performance metadata",
                             DEFAULT_PERFORMANCE_OVERLAY_ENABLED,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(PropertyId::BgImage),
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
    self->performanceOverlayEnabled = DEFAULT_PERFORMANCE_OVERLAY_ENABLED;
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

    switch (static_cast<PropertyId>(prop_id)) {
    case PropertyId::Enabled:
        self->enabled = g_value_get_boolean(value);
        GST_INFO_OBJECT(self, "Enabled set to: %d", self->enabled);
        break;

    case PropertyId::PerformanceOverlayEnabled:
        self->performanceOverlayEnabled = g_value_get_boolean(value);
        GST_INFO_OBJECT(
            self, "Performance overlay enabled set to: %d", self->performanceOverlayEnabled);
        break;

    case PropertyId::BgImage:
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

    switch (static_cast<PropertyId>(prop_id)) {
    case PropertyId::Enabled:
        g_value_set_boolean(value, self->enabled);
        break;

    case PropertyId::PerformanceOverlayEnabled:
        g_value_set_boolean(value, self->performanceOverlayEnabled);
        break;

    case PropertyId::BgImage:
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

static std::unique_ptr<Osd::Layer>
drawPerformanceLayer([[maybe_unused]] GstPekOsd *self,
                     float imgWidth,
                     float imgHeight,
                     const perception::FrameResults &frameResults) {
    constexpr float line_height = 16.0f;
    constexpr float x_offset = 10.0f;
    constexpr float y_offset = 10.0f;

    auto layer = std::make_unique<pek::osd::Layer>(imgWidth, imgHeight);

    float line_y_offset = y_offset;
    frameResults.for_each<perception::metadata::PerformanceOverlayT>([&](const auto &payload) {
        for (const auto &line : payload.lines) {
            Osd::Text::draw(*layer,
                            Osd::Coordinate(x_offset, line_y_offset),
                            line,
                            pek::Colors::fromStringOrDefault("#66ff00ff"),
                            pek::Colors::fromStringOrDefault("#000000ff"),
                            "monospace",
                            line_height);
            line_y_offset += line_height;
        }
    });

    return layer;
}

struct SegmentationBitmapView {
    const uint8_t *data = nullptr;
    size_t size = 0;
    size_t width = 0;
    size_t height = 0;
};

static SegmentationBitmapView
makeSegmentationBitmapView(const perception::metadata::BitmapDataT &data) {
    return SegmentationBitmapView{
        data.pixels.data(),
        data.pixels.size(),
        static_cast<size_t>(data.width),
        static_cast<size_t>(data.height),
    };
}

static std::unique_ptr<Osd::Layer> drawSegmentationLayer([[maybe_unused]] GstPekOsd *self,
                                                         float imgWidth,
                                                         float imgHeight,
                                                         const SegmentationBitmapView &segMap) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);

    cairo_surface_flush(layer->surface);
    auto *data = cairo_image_surface_get_data(layer->surface);
    const int stride = cairo_image_surface_get_stride(layer->surface);

    const size_t sw = segMap.width;
    const size_t sh = segMap.height;

    if (segMap.data == nullptr || sw == 0U || sh == 0U || segMap.size < (sw * sh)) {
        return layer;
    }

    const float scale_x = imgWidth / static_cast<float>(sw);
    const float scale_y = imgHeight / static_cast<float>(sh);

    const auto *seg = segMap.data;

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
                              const SegmentationBitmapView &segMap,
                              const std::optional<pek::Bitmap> &bgImage) {
    assert(imgData != nullptr);

    const auto segWidth = segMap.width;
    const auto segHeight = segMap.height;

    if (segMap.data == nullptr || segWidth == 0U || segHeight == 0U ||
        segMap.size < (segWidth * segHeight)) {
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
            const auto maskValue = segMap.data[segY * segWidth + segX];

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

static constexpr const char *HUMAN_FACE_CONTENT_TYPE = "humanFace";
static constexpr const char *GENERIC_OBJECT_CONTENT_TYPE = "genericObject";
static constexpr const char *CLASSIFICATION_CONTENT_TYPE = "classification";
static constexpr const char *PERSON_CLASSIFICATION_CONTENT_TYPE = "personClassification";
static constexpr const char *EYE_YAW_PITCH_CONTENT_TYPE = "eyeYawPitch";
static constexpr const char *CAMERA_CONTACT_CONTENT_TYPE = "cameraContact";
static constexpr const char *SEGMENTATION_CONTENT_TYPE = "segmentation";
static constexpr const char *BACKGROUND_REPLACEMENT_COMPOSITING_MODE = "backgroundReplacement";

static bool hasContentType(const perception::metadata::LayerInfoT *layer, const char *contentType) {
    return layer != nullptr && layer->content_type == contentType;
}

static bool isHumanFaceLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, HUMAN_FACE_CONTENT_TYPE);
}

static bool isGenericObjectLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, GENERIC_OBJECT_CONTENT_TYPE);
}

static bool isClassificationLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, CLASSIFICATION_CONTENT_TYPE);
}

static bool isPersonClassificationLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, PERSON_CLASSIFICATION_CONTENT_TYPE);
}

static bool isEyeYawPitchLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, EYE_YAW_PITCH_CONTENT_TYPE);
}

static bool isCameraContactLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, CAMERA_CONTACT_CONTENT_TYPE);
}

static bool isSegmentationLayer(const perception::metadata::LayerInfoT *layer) {
    return hasContentType(layer, SEGMENTATION_CONTENT_TYPE);
}

static bool usesBackgroundReplacement(const perception::metadata::LayerInfoT *layer) {
    return layer != nullptr && layer->compositing_mode == BACKGROUND_REPLACEMENT_COMPOSITING_MODE;
}

static const perception::metadata::BoxDetectionT *
findFirstHumanFaceDetection(const perception::FrameResults &frameResults, uint64_t id) {
    const perception::metadata::BoxDetectionT *parent = nullptr;

    frameResults.for_each<perception::metadata::BoxDetectionsT>([&](const auto &payload) {
        if (parent != nullptr || !isHumanFaceLayer(payload.layer.get())) {
            return;
        }

        for (const auto &det : payload.detections) {
            if (!det || !det->object || !det->box) {
                continue;
            }
            if (id != 0U && det->object->id != id) {
                continue;
            }

            parent = det.get();
            return;
        }
    });

    return parent;
}

static const perception::metadata::BoxDetectionT *
findOnlyHumanFaceDetection(const perception::FrameResults &frameResults, uint64_t id) {
    const perception::metadata::BoxDetectionT *parent = nullptr;
    size_t parentCount = 0U;

    frameResults.for_each<perception::metadata::BoxDetectionsT>([&](const auto &payload) {
        if (!isHumanFaceLayer(payload.layer.get())) {
            return;
        }

        for (const auto &det : payload.detections) {
            if (!det || !det->object || !det->box) {
                continue;
            }
            if (id != 0U && det->object->id != id) {
                continue;
            }

            parent = det.get();
            ++parentCount;
        }
    });

    return parentCount == 1U ? parent : nullptr;
}

static void drawGazeVectors(Osd::Layer *layer, const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::PoseEstimationsT>([&](const auto &payload) {
        if (!isEyeYawPitchLayer(payload.layer.get())) {
            return;
        }

        for (const auto &pose : payload.poses) {
            if (!pose || !pose->object) {
                continue;
            }

            const auto *parent = findOnlyHumanFaceDetection(frameResults, pose->object->parent_id);
            if (parent == nullptr) {
                continue;
            }

            const auto *parentBox = parent->box.get();
            if (parentBox == nullptr) {
                continue;
            }

            const float x = parentBox->x + parentBox->width / 2.0f;
            const float y = parentBox->y + parentBox->height / 2.0f;
            const float yaw = pose->yaw;
            const float pitch = pose->pitch;

            if (yaw < 0.1f && pitch < 0.1f && yaw > -0.1f && pitch > -0.1f) {
                continue;
            }

            float xEnd = x;
            float yEnd = y;
            gazeEndpoint(x, y, yaw, pitch, 120.0f, xEnd, yEnd);
            Osd::Arrow::draw(*layer, {x, y}, {xEnd, yEnd}, pek::Colors::lightGoldenrodYellow);
        }
    });
}

static void drawCameraContactMarkers(Osd::Layer *layer,
                                     const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ClassificationsT>([&](const auto &payload) {
        if (!isCameraContactLayer(payload.layer.get())) {
            return;
        }

        for (const auto &classification : payload.classifications) {
            if (!classification || !classification->object || classification->candidates.empty() ||
                !classification->candidates.front()) {
                continue;
            }

            const auto &candidate = *classification->candidates.front();
            if (candidate.class_id < 0) {
                continue;
            }

            const auto *parent =
                findFirstHumanFaceDetection(frameResults, classification->object->parent_id);
            if (parent == nullptr) {
                continue;
            }

            const auto *face = parent->box.get();
            if (face == nullptr) {
                continue;
            }

            const float x = face->x + face->width * 0.5f;
            const float y = face->y + face->height * 0.5f;
            const bool hasCameraContact = candidate.class_id == 1;
            const pek::Color markerColor = hasCameraContact ? pek::Colors::lime : pek::Colors::red;
            const float baseRadius = std::min(face->width, face->height) * 0.5f;
            const float markerRadius = hasCameraContact
                                           ? std::clamp(baseRadius * 0.65f, 18.0f, 80.0f)
                                           : std::clamp(baseRadius * 1.15f, 28.0f, 140.0f);
            const float markerThickness = hasCameraContact ? 5.0f : 8.0f;
            const float centerPointSize = hasCameraContact ? 10.0f : 14.0f;

            pek::osd::Circle::draw(
                *layer, pek::osd::Coordinate{x, y}, markerRadius, markerColor, markerThickness);
            pek::osd::Point::draw(*layer, pek::osd::Coordinate{x, y}, markerColor, centerPointSize);
        }
    });
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

static void drawTrackTrace(Osd::Layer &layer, const perception::metadata::TrackTraceT &trace) {
    if (trace.points.size() < 2U) {
        return;
    }

    const auto baseColor = colorForTrack(trace.track_id);
    const auto segmentCount = trace.points.size() - 1U;

    for (size_t i = 0; i < segmentCount; ++i) {
        const auto &from = trace.points[i];
        const auto &to = trace.points[i + 1U];
        if (!from || !to) {
            continue;
        }

        const auto normalizedAge = static_cast<float>(i + 1U) / static_cast<float>(segmentCount);
        const auto alpha = 0.35f + (0.65f * normalizedAge);

        Osd::Arrow::draw(layer,
                         Osd::Coordinate{from->x, from->y},
                         Osd::Coordinate{to->x, to->y},
                         withAlpha(baseColor, alpha),
                         4.0f,
                         0.0f,
                         0.0f);
    }
}

struct TrackedSourceIds {
    std::set<uint64_t> humanFaces;
    std::set<uint64_t> genericObjects;
};

static std::set<uint64_t> *trackedSourceSetForLayer(TrackedSourceIds &ids,
                                                    const perception::metadata::LayerInfoT *layer) {
    if (isHumanFaceLayer(layer)) {
        return &ids.humanFaces;
    }
    if (isGenericObjectLayer(layer)) {
        return &ids.genericObjects;
    }
    return nullptr;
}

static TrackedSourceIds collectTrackedSourceIds(const perception::FrameResults &frameResults) {
    TrackedSourceIds result;
    frameResults.for_each<perception::metadata::ObjectTracksT>([&](const auto &payload) {
        auto *sourceIds = trackedSourceSetForLayer(result, payload.layer.get());
        if (sourceIds == nullptr) {
            return;
        }

        for (const auto &track : payload.tracks) {
            if (track && track->source_id != 0U) {
                sourceIds->insert(track->source_id);
            }
        }
    });
    return result;
}

static bool isTrackedSourceDetection(const perception::metadata::BoxDetectionT &box,
                                     const std::set<uint64_t> &trackedSourceIds) {
    if (!box.object) {
        return false;
    }

    return trackedSourceIds.find(box.object->id) != trackedSourceIds.end();
}

static void drawHumanFaceDetection(Osd::Layer &layer,
                                   const perception::metadata::BoxDetectionT &box) {
    if (!box.box) {
        return;
    }

    const auto &b = *box.box;
    Osd::Circle::draw(layer,
                      Osd::Coordinate{b.x + b.width / 2.0f, b.y + b.height / 2.0f},
                      b.width / 2.0f,
                      pek::Colors::fromStringOrDefault("#2600ffff"),
                      2.0f);
}

static void drawGenericObjectDetection(Osd::Layer &layer,
                                       const perception::metadata::BoxDetectionT &box) {
    if (!box.box) {
        return;
    }

    Osd::ObjectBox::draw(layer, box, pek::Colors::fromStringOrDefault("#ff0000ff"), 2.0f);
}

static perception::metadata::BoxDetectionT
trackAsDetection(const perception::metadata::ObjectTrackT &track) {
    perception::metadata::BoxDetectionT detection;
    detection.object = track.object
                           ? std::make_unique<perception::metadata::ObjectMetaT>(*track.object)
                           : perception::makeObjectMeta(0U, track.source_id);
    detection.box =
        track.box ? std::make_unique<perception::metadata::BoundingBoxT>(*track.box) : nullptr;
    detection.confidence = track.confidence;
    detection.class_id = track.class_id;
    detection.text = track.text;
    return detection;
}

static void drawPersonPresence(Osd::Layer &layer,
                               float imgWidth,
                               float imgHeight,
                               const perception::metadata::PersonPresenceT &presence) {
    const bool isPerson = presence.yes_confidence > presence.no_confidence;
    const std::string label = isPerson ? "PERSON" : "NON-PERSON";
    const auto color = isPerson ? pek::Colors::fromStringOrDefault("#66ff00ff")
                                : pek::Colors::fromStringOrDefault("#ff4444ff");
    constexpr float fontSize = 64.0f;

    cairo_select_font_face(
        layer.context, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(layer.context, fontSize);
    cairo_text_extents_t ex;
    cairo_text_extents(layer.context, label.c_str(), &ex);

    const float x = (imgWidth - ex.width) / 2.0f - ex.x_bearing;
    const float y = (imgHeight - ex.height) / 2.0f - ex.y_bearing;

    uint64_t timeMs = pek::Time::utcMs();
    if (timeMs % 1000 < 800) {
        Osd::Text::draw(layer,
                        Osd::Coordinate(x, y),
                        label,
                        color,
                        pek::Colors::fromStringOrDefault("#000000cc"),
                        "monospace",
                        fontSize);
    }
}

static void drawClassificationList(Osd::Layer &layer,
                                   float imgHeight,
                                   const perception::metadata::ClassificationT &classification) {
    const float fontSize = 14.0f;
    const float lineHeight = fontSize * 1.5f;
    const auto numResults = classification.candidates.size();
    const float padding = 10.0f;
    const float startX = padding;
    const float startY =
        imgHeight - (static_cast<float>(numResults) * lineHeight) - (2.0f * padding);

    for (size_t i = 0U; i < classification.candidates.size(); ++i) {
        const auto &result = classification.candidates[i];
        if (!result) {
            continue;
        }

        std::ostringstream oss;
        oss << "#" << (i + 1U) << ": " << result->text << " (" << std::fixed << std::setprecision(1)
            << (result->confidence * 100.0f) << "%)";

        const float textX = startX;
        const float textY = startY + static_cast<float>(i) * lineHeight;

        Osd::Text::draw(layer,
                        Osd::Coordinate(textX, textY),
                        oss.str(),
                        pek::Colors::fromStringOrDefault("#ffffffff"),
                        pek::Colors::fromStringOrDefault("#000000ff"),
                        "monospace",
                        fontSize);
    }
}

static void drawHumanFaceDetections(Osd::Layer &layer,
                                    const perception::metadata::BoxDetectionsT &payload,
                                    const TrackedSourceIds &trackedSourceIds) {
    if (!isHumanFaceLayer(payload.layer.get())) {
        return;
    }

    for (const auto &box : payload.detections) {
        if (box && !isTrackedSourceDetection(*box, trackedSourceIds.humanFaces)) {
            drawHumanFaceDetection(layer, *box);
        }
    }
}

static void drawGenericObjectDetections(Osd::Layer &layer,
                                        const perception::metadata::BoxDetectionsT &payload,
                                        const TrackedSourceIds &trackedSourceIds) {
    if (!isGenericObjectLayer(payload.layer.get())) {
        return;
    }

    for (const auto &box : payload.detections) {
        if (box && !isTrackedSourceDetection(*box, trackedSourceIds.genericObjects)) {
            drawGenericObjectDetection(layer, *box);
        }
    }
}

static void drawBoxDetections(Osd::Layer &layer,
                              const perception::FrameResults &frameResults,
                              const TrackedSourceIds &trackedSourceIds) {
    frameResults.for_each<perception::metadata::BoxDetectionsT>([&](const auto &payload) {
        drawHumanFaceDetections(layer, payload, trackedSourceIds);
        drawGenericObjectDetections(layer, payload, trackedSourceIds);
    });
}

static void drawHumanFaceTracks(Osd::Layer &layer,
                                const perception::metadata::ObjectTracksT &payload) {
    if (!isHumanFaceLayer(payload.layer.get())) {
        return;
    }

    for (const auto &track : payload.tracks) {
        if (track) {
            drawHumanFaceDetection(layer, trackAsDetection(*track));
        }
    }
}

static void drawGenericObjectTracks(Osd::Layer &layer,
                                    const perception::metadata::ObjectTracksT &payload) {
    if (!isGenericObjectLayer(payload.layer.get())) {
        return;
    }

    for (const auto &track : payload.tracks) {
        if (track) {
            drawGenericObjectDetection(layer, trackAsDetection(*track));
        }
    }
}

static void drawObjectTracks(Osd::Layer &layer, const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ObjectTracksT>([&](const auto &payload) {
        drawHumanFaceTracks(layer, payload);
        drawGenericObjectTracks(layer, payload);
    });
}

static void drawPersonClassifications(Osd::Layer &layer,
                                      float imgWidth,
                                      float imgHeight,
                                      const perception::metadata::ClassificationsT &payload) {
    if (!isPersonClassificationLayer(payload.layer.get())) {
        return;
    }

    for (const auto &presence : payload.person_presence) {
        if (presence) {
            drawPersonPresence(layer, imgWidth, imgHeight, *presence);
        }
    }
}

static void drawImageClassifications(Osd::Layer &layer,
                                     float imgHeight,
                                     const perception::metadata::ClassificationsT &payload) {
    if (!isClassificationLayer(payload.layer.get())) {
        return;
    }

    for (const auto &classification : payload.classifications) {
        if (classification) {
            drawClassificationList(layer, imgHeight, *classification);
        }
    }
}

static void drawClassifications(Osd::Layer &layer,
                                float imgWidth,
                                float imgHeight,
                                const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ClassificationsT>([&](const auto &payload) {
        drawPersonClassifications(layer, imgWidth, imgHeight, payload);
        drawImageClassifications(layer, imgHeight, payload);
    });
}

static std::unique_ptr<Osd::Layer>
drawFrameResultsLayer([[maybe_unused]] GstPekOsd *self,
                      float imgWidth,
                      float imgHeight,
                      const perception::FrameResults &frameResults) {
    auto layer = std::make_unique<Osd::Layer>(imgWidth, imgHeight);
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults);

    frameResults.for_each<perception::metadata::TrackTracesT>([&](const auto &payload) {
        for (const auto &trace : payload.traces) {
            if (trace) {
                drawTrackTrace(*layer, *trace);
            }
        }
    });

    drawBoxDetections(*layer, frameResults, trackedSourceIds);
    drawObjectTracks(*layer, frameResults);
    drawClassifications(*layer, imgWidth, imgHeight, frameResults);
    drawGazeVectors(layer.get(), frameResults);
    drawCameraContactMarkers(layer.get(), frameResults);

    return layer;
}

static void gst_pek_osd_process_segmentation(GstPekOsd *self,
                                             guint8 *imgData,
                                             gint imgStride,
                                             float imgWidth,
                                             float imgHeight,
                                             Osd::Layers_t &layers,
                                             const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::SegmentationMasksT>([&](const auto &payload) {
        if (!isSegmentationLayer(payload.layer.get())) {
            return;
        }

        const bool useBackgroundReplacement = usesBackgroundReplacement(payload.layer.get());

        for (const auto &mask : payload.masks) {
            if (!mask || !mask->bitmap || mask->bitmap->pixels.empty() ||
                mask->bitmap->width == 0U || mask->bitmap->height == 0U) {
                continue;
            }

            const auto bitmap = makeSegmentationBitmapView(*mask->bitmap);
            if (useBackgroundReplacement) {
                replaceBackground(imgData,
                                  static_cast<gint>(imgWidth),
                                  static_cast<gint>(imgHeight),
                                  imgStride,
                                  bitmap,
                                  self->bgImage);
            } else {
                layers.push_back(drawSegmentationLayer(self, imgWidth, imgHeight, bitmap));
            }
        }
    });
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

    if (auto frameResults = pek::FrameResultsMeta::read(frame->buffer); frameResults != nullptr) {
        gst_pek_osd_process_segmentation(
            self, imgData, imgStride, imgWidth, imgHeight, layers, *frameResults);
        layers.push_back(drawFrameResultsLayer(self, imgWidth, imgHeight, *frameResults));
        if (self->performanceOverlayEnabled) {
            layers.push_back(drawPerformanceLayer(self, imgWidth, imgHeight, *frameResults));
        }

        pek::osd::Canvas(imgData, imgWidth, imgHeight).paint(layers);
    }

    ++self->frameCount;
    return GST_FLOW_OK;
}

static gboolean pekosd_plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_pek_osd_debug, "pekosd", 0, "PEK OSD Overlay");

    return gst_element_register(plugin, "pekosd", GST_RANK_NONE, GST_TYPE_PEK_OSD);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  pekosd,
                  "PEK OSD Overlay - On-Screen Display for BGRA video frames",
                  pekosd_plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
