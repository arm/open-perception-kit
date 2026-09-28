/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "DebugOverlayRenderer.h"
#include "gst/FrameResultsMeta.h"
#include "opk/Bitmap.h"
#include "opk/FrameResults.h"
#include "opk/Tools.h"
#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#ifndef PACKAGE
#define PACKAGE "opk-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_OPK_OSD (gst_opk_osd_get_type())
#define GST_OPK_OSD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OPK_OSD, GstOpkOsd))
#define GST_OPK_OSD_CLASS(klass)                                                                   \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OPK_OSD, GstOpkOsdClass))
#define GST_IS_OPK_OSD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OPK_OSD))
#define GST_IS_OPK_OSD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OPK_OSD))

typedef struct _GstOpkOsd GstOpkOsd;
typedef struct _GstOpkOsdClass GstOpkOsdClass;

struct _GstOpkOsd {
    GstVideoFilter videofilter;

    // Properties
    gboolean enabled;
    gboolean performanceOverlayEnabled;
    gchar *bgImagePath;

    // Internal state
    guint frameCount;
    std::optional<opk::Bitmap> bgImage;
};

struct _GstOpkOsdClass {
    GstVideoFilterClass parent_class;
};

GType gst_opk_osd_get_type(void);

G_END_DECLS

GST_DEBUG_CATEGORY_STATIC(gst_opk_osd_debug);
#define GST_CAT_DEFAULT gst_opk_osd_debug

namespace Osd = opk::osd;

static constexpr const char *OPK_SUPPORTED_RAW_VIDEO_CAPS =
    "video/x-raw, format=(string){BGRA,RGB,I420,NV12,YUY2}";

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
gst_opk_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void
gst_opk_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_opk_osd_finalize(GObject *object);
static GstFlowReturn gst_opk_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame);
static gboolean gst_opk_osd_start(GstBaseTransform *trans);
static gboolean gst_opk_osd_stop(GstBaseTransform *trans);
static bool gst_opk_osd_load_bg_image(GstOpkOsd *self);

#define gst_opk_osd_parent_class parent_class
G_DEFINE_TYPE(GstOpkOsd, gst_opk_osd, GST_TYPE_VIDEO_FILTER);

static void gst_opk_osd_class_init(GstOpkOsdClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *vfilter_class = GST_VIDEO_FILTER_CLASS(klass);
    GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_opk_osd_set_property;
    gobject_class->get_property = gst_opk_osd_get_property;
    gobject_class->finalize = gst_opk_osd_finalize;

    vfilter_class->transform_frame_ip = gst_opk_osd_transform_frame_ip;
    trans_class->start = gst_opk_osd_start;
    trans_class->stop = gst_opk_osd_stop;

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

    gst_element_class_set_static_metadata(element_class,
                                          "OPK OSD Overlay",
                                          "Filter/Video",
                                          "On-Screen Display overlay for OPK video frames",
                                          "OPK Development Team");

    GstCaps *caps = gst_caps_from_string(OPK_SUPPORTED_RAW_VIDEO_CAPS);
    GstPadTemplate *src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    GstPadTemplate *sink_template =
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template(element_class, src_template);
    gst_element_class_add_pad_template(element_class, sink_template);
    gst_caps_unref(caps);
}

static void gst_opk_osd_init(GstOpkOsd *self) {
    self->enabled = DEFAULT_ENABLED;
    self->performanceOverlayEnabled = DEFAULT_PERFORMANCE_OVERLAY_ENABLED;
    self->bgImagePath = g_strdup(DEFAULT_BG_IMAGE);
    self->frameCount = 0;
    std::construct_at(&self->bgImage);

    GST_DEBUG_OBJECT(self, "Initialized OpkOsd element");
}

static bool gst_opk_osd_load_bg_image(GstOpkOsd *self) {
    self->bgImage.reset();

    if (self->bgImagePath == nullptr || self->bgImagePath[0] == '\0') {
        GST_DEBUG_OBJECT(self, "No bg-image configured");
        return false;
    }

    size_t imageWidth = 0;
    size_t imageHeight = 0;
    auto loadedPixels = opk::Tools::loadImageFile(self->bgImagePath, imageWidth, imageHeight);
    if (!loadedPixels) {
        GST_WARNING_OBJECT(self,
                           "Failed to load bg-image '%s': %s",
                           self->bgImagePath,
                           loadedPixels.error().toString().c_str());
        return false;
    }

    constexpr size_t srcBytesPerPixel = 3U;
    constexpr size_t dstBytesPerPixel = 4U;

    if (imageWidth == 0U || imageHeight == 0U ||
        imageWidth > std::numeric_limits<size_t>::max() / imageHeight ||
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

    opk::Bitmap bitmap;
    bitmap.realloc(opk::Bitmap::Type::Uint32, imageWidth, imageHeight);

    std::span<const uint8_t> srcPixels(pixels.data(), pixels.size());
    std::span<uint8_t> dstPixels(bitmap.getMutableData(), dstByteCount);

    for (size_t y = 0; y < imageHeight; ++y) {
        for (size_t x = 0; x < imageWidth; ++x) {
            const size_t srcIdx = (y * imageWidth + x) * srcBytesPerPixel;
            const size_t dstIdx = (y * imageWidth + x) * dstBytesPerPixel;

            // Store as BGRA for the raster background replacement helper.
            dstPixels[dstIdx + 0U] = srcPixels[srcIdx + 2U];
            dstPixels[dstIdx + 1U] = srcPixels[srcIdx + 1U];
            dstPixels[dstIdx + 2U] = srcPixels[srcIdx + 0U];
            dstPixels[dstIdx + 3U] = 255U;
        }
    }

    self->bgImage = std::move(bitmap);

    GST_INFO_OBJECT(
        self, "Loaded bg-image '%s' (%zux%zu)", self->bgImagePath, imageWidth, imageHeight);
    return true;
}

static void
gst_opk_osd_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto *self = GST_OPK_OSD(object);

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
        gst_opk_osd_load_bg_image(self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_opk_osd_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto *self = GST_OPK_OSD(object);

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

static void gst_opk_osd_finalize(GObject *object) {
    auto *self = GST_OPK_OSD(object);

    GST_DEBUG_OBJECT(self, "Finalizing OpkOsd element");

    g_free(self->bgImagePath);
    self->bgImagePath = nullptr;
    std::destroy_at(&self->bgImage);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_opk_osd_start(GstBaseTransform *trans) {
    auto *self = GST_OPK_OSD(trans);

    GST_INFO_OBJECT(self, "Starting OpkOsd element");
    self->frameCount = 0;
    gst_opk_osd_load_bg_image(self);

    return TRUE;
}

static gboolean gst_opk_osd_stop(GstBaseTransform *trans) {
    auto *self = GST_OPK_OSD(trans);

    GST_INFO_OBJECT(self, "Stopping OpkOsd element - processed %u frames", self->frameCount);

    return TRUE;
}

static opk::RawImagePixelFormat rawImagePixelFormatFromGst(GstVideoFormat format) noexcept {
    using enum opk::RawImagePixelFormat;

    switch (format) {
    case GST_VIDEO_FORMAT_BGRA:
        return Bgra;
    case GST_VIDEO_FORMAT_RGB:
        return Rgb;
    case GST_VIDEO_FORMAT_I420:
        return I420;
    case GST_VIDEO_FORMAT_NV12:
        return Nv12;
    case GST_VIDEO_FORMAT_YUY2:
        return Yuy2;
    default:
        return Unknown;
    }
}

static bool isYuvFormat(opk::RawImagePixelFormat format) noexcept {
    using enum opk::RawImagePixelFormat;

    switch (format) {
    case I420:
    case Nv12:
    case Yuy2:
        return true;
    default:
        return false;
    }
}

static opk::YuvColorMatrix defaultYuvColorMatrix(std::uint32_t height) noexcept {
    using enum opk::YuvColorMatrix;

    return height <= 576U ? Bt601 : Bt709;
}

static opk::YuvColorMatrix yuvColorMatrixFromGst(const GstVideoColorimetry &colorimetry,
                                                 std::uint32_t height) noexcept {
    using enum opk::YuvColorMatrix;

    switch (colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_BT601:
        return Bt601;
    case GST_VIDEO_COLOR_MATRIX_BT709:
        return Bt709;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
        return Bt2020;
    case GST_VIDEO_COLOR_MATRIX_UNKNOWN:
        return defaultYuvColorMatrix(height);
    default:
        return Unknown;
    }
}

static opk::YuvRange yuvRangeFromGst(const GstVideoColorimetry &colorimetry) noexcept {
    using enum opk::YuvRange;

    switch (colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
        return Full;
    case GST_VIDEO_COLOR_RANGE_16_235:
        return Limited;
    case GST_VIDEO_COLOR_RANGE_UNKNOWN:
        return Limited;
    default:
        return Unknown;
    }
}

static std::size_t debugOverlayPlaneByteSize(const GstVideoFrame &frame, guint plane) noexcept {
    const auto &info = frame.info;
    if (info.finfo == nullptr) {
        return 0U;
    }

    const gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, plane);
    const guint height =
        GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT(info.finfo, plane, GST_VIDEO_INFO_HEIGHT(&info));
    if (stride <= 0 || height == 0U) {
        return 0U;
    }

    const auto strideBytes = static_cast<std::size_t>(stride);
    if (strideBytes > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height)) {
        return 0U;
    }

    return strideBytes * static_cast<std::size_t>(height);
}

static Osd::DebugOverlayRequest
makeDebugOverlayRequest(GstOpkOsd *self,
                        const GstVideoFrame *frame,
                        const open_perception_kit::FrameResults &frameResults,
                        std::array<opk::ImagePlaneDesc, opk::MaxImagePlaneCount> &planes) noexcept {
    const auto &info = frame->info;
    const auto format = rawImagePixelFormatFromGst(GST_VIDEO_INFO_FORMAT(&info));
    const auto width = static_cast<std::uint32_t>(GST_VIDEO_INFO_WIDTH(&info));
    const auto height = static_cast<std::uint32_t>(GST_VIDEO_INFO_HEIGHT(&info));
    const auto planeCount = std::min<std::size_t>(GST_VIDEO_INFO_N_PLANES(&info), planes.size());

    for (std::size_t plane = 0U; plane < planeCount; ++plane) {
        auto *data = static_cast<std::uint8_t *>(
            GST_VIDEO_FRAME_PLANE_DATA(frame, static_cast<guint>(plane)));
        const gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, static_cast<guint>(plane));
        const auto byteCount = debugOverlayPlaneByteSize(*frame, static_cast<guint>(plane));
        if (data == nullptr || stride <= 0 || byteCount == 0U) {
            planes[plane] = {};
            continue;
        }

        planes[plane] = {
            data,
            data,
            byteCount,
            static_cast<std::size_t>(stride),
        };
    }

    const auto yuvMatrix = isYuvFormat(format)
                               ? yuvColorMatrixFromGst(GST_VIDEO_INFO_COLORIMETRY(&info), height)
                               : opk::YuvColorMatrix::Unknown;
    const auto yuvRange = isYuvFormat(format) ? yuvRangeFromGst(GST_VIDEO_INFO_COLORIMETRY(&info))
                                              : opk::YuvRange::Unknown;

    return Osd::DebugOverlayRequest{
        .surface =
            Osd::DebugOverlaySurface{
                .format = format,
                .width = width,
                .height = height,
                .planes = std::span<opk::ImagePlaneDesc>(planes.data(), planeCount),
                .yuvMatrix = yuvMatrix,
                .yuvRange = yuvRange,
            },
        .frameResults = &frameResults,
        .options =
            Osd::DebugOverlayOptions{
                .performanceOverlayEnabled = self->performanceOverlayEnabled ? true : false,
                .backgroundImage = self->bgImage ? &*self->bgImage : nullptr,
            },
    };
}

static const char *debugOverlayStatusName(Osd::DebugOverlayStatus status) noexcept {
    using enum Osd::DebugOverlayStatus;

    switch (status) {
    case Drawn:
        return "drawn";
    case MissingFrameResults:
        return "missing-frame-results";
    case UnsupportedFormat:
        return "unsupported-format";
    case InvalidSurface:
        return "invalid-surface";
    default:
        return "unknown";
    }
}

static GstFlowReturn gst_opk_osd_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame) {
    auto *self = GST_OPK_OSD(filter);

    if (!self->enabled) {
        return GST_FLOW_OK;
    }

    OPK_PERF_SCOPE("osd/render");

    if (auto frameResults = opk::FrameResultsMeta::read(frame->buffer); frameResults != nullptr) {
        std::array<opk::ImagePlaneDesc, opk::MaxImagePlaneCount> debugOverlayPlanes{};
        const auto request =
            makeDebugOverlayRequest(self, frame, *frameResults, debugOverlayPlanes);
        const auto status = Osd::drawDebugOverlay(request);
        if (status == Osd::DebugOverlayStatus::InvalidSurface) {
            GST_ELEMENT_ERROR(self,
                              RESOURCE,
                              READ,
                              ("Failed to access video frame data"),
                              ("Debug overlay surface is invalid"));
            return GST_FLOW_ERROR;
        }
        if (status != Osd::DebugOverlayStatus::Drawn) {
            GST_WARNING_OBJECT(self, "Debug overlay failed: %s", debugOverlayStatusName(status));
        }
    }

    ++self->frameCount;
    return GST_FLOW_OK;
}

static gboolean opkosd_plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_opk_osd_debug, "opkosd", 0, "OPK OSD Overlay");

    return gst_element_register(plugin, "opkosd", GST_RANK_NONE, GST_TYPE_OPK_OSD);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opkosd,
                  "OPK OSD Overlay - On-Screen Display for OPK video frames",
                  opkosd_plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
