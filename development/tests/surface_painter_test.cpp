/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "RasterOsd.h"
#include "mediaio/PixelBufferVideoFrame.h"
#include "pek/Tools.h"
#include "raster/BitmapFont.h"
#include "raster/SegmentationMask.h"
#include "raster/SurfacePainter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using pek::ImagePlaneDesc;
using pek::RawImagePixelFormat;
using pek::raster::BitmapFont;
using pek::raster::SurfacePainter;
using pek::raster::TextAnchor;

constexpr std::uint8_t Sentinel = 0xcd;

struct PlaneStorage {
    std::vector<std::uint8_t> bytes;
    std::uint32_t stride = 0;
    std::uint32_t rowBytes = 0;
    std::uint32_t height = 0;
};

struct SurfaceStorage {
    RawImagePixelFormat format = RawImagePixelFormat::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::array<PlaneStorage, pek::MaxImagePlaneCount> storage;
    std::array<ImagePlaneDesc, pek::MaxImagePlaneCount> planes{};
    std::size_t planeCount = 0;
};

PlaneStorage makePlane(std::uint32_t rowBytes, std::uint32_t height, std::uint32_t padding = 3) {
    PlaneStorage plane;
    plane.rowBytes = rowBytes;
    plane.height = height;
    plane.stride = rowBytes + padding;
    plane.bytes.assign(static_cast<std::size_t>(plane.stride) * height, Sentinel);
    return plane;
}

SurfaceStorage makeSurface(RawImagePixelFormat format, std::uint32_t width, std::uint32_t height) {
    SurfaceStorage surface;
    surface.format = format;
    surface.width = width;
    surface.height = height;

    const std::uint32_t chromaWidth = (width + 1) / 2;
    const std::uint32_t chromaHeight = (height + 1) / 2;
    switch (format) {
    case RawImagePixelFormat::Bgra:
        surface.planeCount = 1;
        surface.storage[0] = makePlane(width * 4, height);
        break;
    case RawImagePixelFormat::Rgb:
        surface.planeCount = 1;
        surface.storage[0] = makePlane(width * 3, height);
        break;
    case RawImagePixelFormat::I420:
        surface.planeCount = 3;
        surface.storage[0] = makePlane(width, height);
        surface.storage[1] = makePlane(chromaWidth, chromaHeight);
        surface.storage[2] = makePlane(chromaWidth, chromaHeight);
        break;
    case RawImagePixelFormat::Nv12:
        surface.planeCount = 2;
        surface.storage[0] = makePlane(width, height);
        surface.storage[1] = makePlane(chromaWidth * 2, chromaHeight);
        break;
    case RawImagePixelFormat::Yuy2:
        surface.planeCount = 1;
        surface.storage[0] = makePlane(chromaWidth * 4, height);
        break;
    default:
        break;
    }

    for (std::size_t index = 0; index < surface.planeCount; ++index) {
        auto &plane = surface.storage[index];
        surface.planes[index] = {nullptr, plane.bytes.data(), plane.bytes.size(), plane.stride};
    }
    return surface;
}

std::span<ImagePlaneDesc> planeSpan(SurfaceStorage &surface) {
    return std::span<ImagePlaneDesc>(surface.planes.data(), surface.planeCount);
}

bool activeAreaChanged(const SurfaceStorage &surface) {
    for (std::size_t planeIndex = 0; planeIndex < surface.planeCount; ++planeIndex) {
        const auto &plane = surface.storage[planeIndex];
        for (std::uint32_t y = 0; y < plane.height; ++y) {
            const auto *row = plane.bytes.data() + static_cast<std::size_t>(y) * plane.stride;
            for (std::uint32_t x = 0; x < plane.rowBytes; ++x) {
                if (row[x] != Sentinel) {
                    return true;
                }
            }
        }
    }
    return false;
}

void expectPaddingUnchanged(const SurfaceStorage &surface) {
    for (std::size_t planeIndex = 0; planeIndex < surface.planeCount; ++planeIndex) {
        const auto &plane = surface.storage[planeIndex];
        for (std::uint32_t y = 0; y < plane.height; ++y) {
            const auto *row = plane.bytes.data() + static_cast<std::size_t>(y) * plane.stride;
            for (std::uint32_t x = plane.rowBytes; x < plane.stride; ++x) {
                EXPECT_EQ(row[x], Sentinel)
                    << "plane=" << planeIndex << " row=" << y << " padding=" << x;
            }
        }
    }
}

std::uint8_t *planeRow(SurfaceStorage &surface, std::size_t planeIndex, std::uint32_t y) {
    auto &plane = surface.storage[planeIndex];
    return plane.bytes.data() + static_cast<std::size_t>(y) * plane.stride;
}

const std::uint8_t *
planeRow(const SurfaceStorage &surface, std::size_t planeIndex, std::uint32_t y) {
    const auto &plane = surface.storage[planeIndex];
    return plane.bytes.data() + static_cast<std::size_t>(y) * plane.stride;
}

void fillActivePlane(SurfaceStorage &surface, std::size_t planeIndex, std::uint8_t value) {
    auto &plane = surface.storage[planeIndex];
    for (std::uint32_t y = 0; y < plane.height; ++y) {
        auto *row = planeRow(surface, planeIndex, y);
        std::fill(row, row + plane.rowBytes, value);
    }
}

std::uint8_t
blendForCoverage(std::uint8_t dst, std::uint8_t src, unsigned covered, unsigned total) {
    const auto alpha = static_cast<unsigned>((covered * 255U + total / 2U) / total);
    const auto invAlpha = 255U - alpha;
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(src) * alpha + static_cast<unsigned>(dst) * invAlpha + 127U) / 255U);
}

int changedBgraRowWidth(const SurfaceStorage &surface, std::uint32_t y) {
    const auto *row = planeRow(surface, 0U, y);
    int firstChanged = -1;
    int lastChanged = -1;
    for (std::uint32_t x = 0; x < surface.width; ++x) {
        const auto pixel = static_cast<std::size_t>(x) * 4U;
        if (row[pixel] == Sentinel && row[pixel + 1U] == Sentinel && row[pixel + 2U] == Sentinel &&
            row[pixel + 3U] == Sentinel) {
            continue;
        }

        if (firstChanged < 0) {
            firstChanged = static_cast<int>(x);
        }
        lastChanged = static_cast<int>(x);
    }

    return firstChanged < 0 ? 0 : lastChanged - firstChanged + 1;
}

std::array<ImagePlaneDesc, 1> readonlyPlanes(const SurfaceStorage &surface) {
    return {ImagePlaneDesc{
        surface.storage[0].bytes.data(),
        nullptr,
        surface.storage[0].bytes.size(),
        surface.storage[0].stride,
    }};
}

void setBgra(pek::Bitmap &bitmap,
             std::size_t x,
             std::size_t y,
             std::uint8_t b,
             std::uint8_t g,
             std::uint8_t r) {
    auto *data = bitmap.getMutableData();
    const auto index = (y * bitmap.getWidth() + x) * 4U;
    data[index + 0U] = b;
    data[index + 1U] = g;
    data[index + 2U] = r;
    data[index + 3U] = 255U;
}

pek::Bitmap makeBackgroundImage() {
    pek::Bitmap bitmap(pek::Bitmap::Type::Uint32, 2, 2);
    setBgra(bitmap, 0, 0, 16, 32, 48);
    setBgra(bitmap, 1, 0, 64, 80, 96);
    setBgra(bitmap, 0, 1, 112, 128, 144);
    setBgra(bitmap, 1, 1, 160, 176, 192);
    return bitmap;
}

std::unique_ptr<perception::metadata::BitmapDataT> makeSegmentationMaskBitmap() {
    auto bitmap = std::make_unique<perception::metadata::BitmapDataT>();
    bitmap->width = 2U;
    bitmap->height = 2U;
    bitmap->value_type = "Uint8";
    bitmap->pixels = {
        0U,
        96U,
        180U,
        255U,
    };
    return bitmap;
}

perception::FrameResults makeSegmentationFrameResults(const char *compositingMode = nullptr) {
    perception::metadata::SegmentationMasksT payload;

    perception::LayerInfoDescriptor layerDescriptor;
    layerDescriptor.contentType = "segmentation";
    if (compositingMode != nullptr) {
        layerDescriptor.compositingMode = compositingMode;
    }
    payload.layer = perception::makeLayerInfo(layerDescriptor);

    auto mask = std::make_unique<perception::metadata::SegmentationMaskT>();
    mask->bitmap = makeSegmentationMaskBitmap();
    payload.masks.push_back(std::move(mask));

    perception::FrameResults frameResults;
    frameResults.add(std::move(payload));
    return frameResults;
}

perception::FrameResults makeTrackTraceFrameResults() {
    perception::metadata::TrackTracesT payload;

    auto trace = std::make_unique<perception::metadata::TrackTraceT>();
    trace->track_id = 3U;

    auto first = std::make_unique<perception::metadata::Point2fT>();
    first->x = 4.0f;
    first->y = 5.0f;
    trace->points.push_back(std::move(first));

    auto second = std::make_unique<perception::metadata::Point2fT>();
    second->x = 36.0f;
    second->y = 18.0f;
    trace->points.push_back(std::move(second));

    auto third = std::make_unique<perception::metadata::Point2fT>();
    third->x = 60.0f;
    third->y = 30.0f;
    trace->points.push_back(std::move(third));

    payload.traces.push_back(std::move(trace));

    perception::FrameResults frameResults;
    frameResults.add(std::move(payload));
    return frameResults;
}

perception::FrameResults makeGenericObjectFrameResults() {
    perception::metadata::BoxDetectionsT payload;

    perception::LayerInfoDescriptor layerDescriptor;
    layerDescriptor.contentType = "genericObject";
    payload.layer = perception::makeLayerInfo(layerDescriptor);

    auto detection = std::make_unique<perception::metadata::BoxDetectionT>();
    detection->object = perception::makeObjectMeta(42U);
    detection->box = perception::makeBoundingBox(-3.4f, 1.2f, 10.8f, 4.6f);
    detection->confidence = 0.8f;
    detection->class_id = 1;
    detection->text = "car";
    payload.detections.push_back(std::move(detection));

    perception::FrameResults frameResults;
    frameResults.add(std::move(payload));
    return frameResults;
}

perception::FrameResults makeHumanFaceAndGazeFrameResults() {
    perception::metadata::BoxDetectionsT facePayload;

    perception::LayerInfoDescriptor faceLayerDescriptor;
    faceLayerDescriptor.contentType = "humanFace";
    facePayload.layer = perception::makeLayerInfo(faceLayerDescriptor);

    auto face = std::make_unique<perception::metadata::BoxDetectionT>();
    face->object = perception::makeObjectMeta(7U);
    face->box = perception::makeBoundingBox(28.0f, 18.0f, 24.0f, 24.0f);
    face->confidence = 0.9f;
    facePayload.detections.push_back(std::move(face));

    perception::metadata::PoseEstimationsT gazePayload;

    perception::LayerInfoDescriptor gazeLayerDescriptor;
    gazeLayerDescriptor.contentType = "eyeYawPitch";
    gazePayload.layer = perception::makeLayerInfo(gazeLayerDescriptor);

    auto gaze = std::make_unique<perception::metadata::PoseEstimationT>();
    gaze->object = perception::makeObjectMeta(8U, 7U);
    gaze->confidence = 0.95f;
    gaze->yaw = 20.0f;
    gaze->pitch = -10.0f;
    gazePayload.poses.push_back(std::move(gaze));

    perception::FrameResults frameResults;
    frameResults.add(std::move(facePayload));
    frameResults.add(std::move(gazePayload));
    return frameResults;
}

perception::FrameResults makeCameraContactFrameResults() {
    perception::metadata::BoxDetectionsT facePayload;

    perception::LayerInfoDescriptor faceLayerDescriptor;
    faceLayerDescriptor.contentType = "humanFace";
    facePayload.layer = perception::makeLayerInfo(faceLayerDescriptor);

    auto face = std::make_unique<perception::metadata::BoxDetectionT>();
    face->object = perception::makeObjectMeta(11U);
    face->box = perception::makeBoundingBox(34.0f, 16.0f, 60.0f, 60.0f);
    face->confidence = 0.9f;
    facePayload.detections.push_back(std::move(face));

    perception::metadata::ClassificationsT contactPayload;

    perception::LayerInfoDescriptor contactLayerDescriptor;
    contactLayerDescriptor.contentType = "cameraContact";
    contactPayload.layer = perception::makeLayerInfo(contactLayerDescriptor);

    auto classification = std::make_unique<perception::metadata::ClassificationT>();
    classification->object = perception::makeObjectMeta(12U, 11U);

    auto candidate = std::make_unique<perception::metadata::ClassificationCandidateT>();
    candidate->confidence = 0.95f;
    candidate->class_id = 1;
    candidate->text = "contact";
    classification->candidates.push_back(std::move(candidate));
    contactPayload.classifications.push_back(std::move(classification));

    perception::FrameResults frameResults;
    frameResults.add(std::move(facePayload));
    frameResults.add(std::move(contactPayload));
    return frameResults;
}

perception::FrameResults makePersonClassificationFrameResults() {
    perception::metadata::ClassificationsT payload;

    perception::LayerInfoDescriptor layerDescriptor;
    layerDescriptor.contentType = "personClassification";
    payload.layer = perception::makeLayerInfo(layerDescriptor);

    auto presence = std::make_unique<perception::metadata::PersonPresenceT>();
    presence->object = perception::makeObjectMeta(21U);
    presence->yes_confidence = 0.9f;
    presence->no_confidence = 0.1f;
    payload.person_presence.push_back(std::move(presence));

    perception::FrameResults frameResults;
    frameResults.add(std::move(payload));
    return frameResults;
}

std::unique_ptr<perception::metadata::ClassificationCandidateT>
makeClassificationCandidate(int classId, const char *text, float confidence) {
    auto candidate = std::make_unique<perception::metadata::ClassificationCandidateT>();
    candidate->class_id = classId;
    candidate->text = text;
    candidate->confidence = confidence;
    return candidate;
}

perception::FrameResults makeImageClassificationFrameResults() {
    perception::metadata::ClassificationsT leftPayload;
    auto leftProducer = perception::makeProducerInfo("left", "test", "cpp-classifier");
    perception::LayerInfoDescriptor leftLayerDescriptor;
    leftLayerDescriptor.contentType = "classification";
    leftLayerDescriptor.producer = leftProducer.get();
    leftPayload.layer = perception::makeLayerInfo(leftLayerDescriptor);

    auto leftClassification = std::make_unique<perception::metadata::ClassificationT>();
    leftClassification->candidates.push_back(makeClassificationCandidate(1, "car", 0.8f));
    leftPayload.classifications.push_back(std::move(leftClassification));

    perception::metadata::ClassificationsT rightPayload;
    auto rightProducer = perception::makeProducerInfo("right", "test", "python-script");
    perception::LayerInfoDescriptor rightLayerDescriptor;
    rightLayerDescriptor.contentType = "classification";
    rightLayerDescriptor.compositingMode = "bottomRight";
    rightLayerDescriptor.producer = rightProducer.get();
    rightPayload.layer = perception::makeLayerInfo(rightLayerDescriptor);

    auto rightClassification = std::make_unique<perception::metadata::ClassificationT>();
    rightClassification->candidates.push_back(makeClassificationCandidate(2, "street", 0.7f));
    rightPayload.classifications.push_back(std::move(rightClassification));

    perception::FrameResults frameResults;
    frameResults.add(std::move(leftPayload));
    frameResults.add(std::move(rightPayload));
    return frameResults;
}

perception::FrameResults makePerformanceOverlayFrameResults() {
    perception::FrameResults frameResults;
    perception::appendPerformanceOverlay(frameResults,
                                         {
                                             "OSD                     :    1.23ms",
                                             "Pipeline                :   60.0 FPS",
                                         });
    return frameResults;
}

void waitForPersonClassificationBlinkOn() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (pek::Time::utcMs() % 1000U >= 800U && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

TEST(BitmapFontTest, RequiredGlyphsAreAvailable) {
    constexpr char required[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ "
                                "%!\"'-+,.?:;()[]{}=<>/\\_#*@|";

    for (const char character : required) {
        if (character == '\0') {
            break;
        }
        EXPECT_TRUE(BitmapFont::hasGlyph(character)) << character;
    }
    EXPECT_FALSE(BitmapFont::hasGlyph('~'));
    EXPECT_EQ(&BitmapFont::glyph('~'), &BitmapFont::glyph('?'));
}

TEST(SurfacePainterTest, RejectsInvalidAndReadOnlyPlanes) {
    auto surface = makeSurface(RawImagePixelFormat::Bgra, 8, 8);
    auto ro = readonlyPlanes(surface);
    SurfacePainter readonly(
        RawImagePixelFormat::Bgra, surface.width, surface.height, std::span<ImagePlaneDesc>(ro));
    EXPECT_FALSE(readonly.valid());
    readonly.drawRect(0, 0, 4, 4, pek::Colors::red);
    readonly.fillRect(0, 0, 4, 4, pek::Colors::lime);
    readonly.drawPoint(2, 2, pek::Colors::blue, 3);
    EXPECT_FALSE(activeAreaChanged(surface));

    SurfacePainter invalidFormat(
        RawImagePixelFormat::Unknown, surface.width, surface.height, planeSpan(surface));
    EXPECT_FALSE(invalidFormat.valid());
}

TEST(SurfacePainterTest, MeasuresTextWithoutSurface) {
    const auto oneScale = SurfacePainter::measureText("OK");
    EXPECT_EQ(oneScale.width, 17);
    EXPECT_EQ(oneScale.height, 12);

    const auto scaled = SurfacePainter::measureText("A+1", 2);
    EXPECT_EQ(scaled.width, 52);
    EXPECT_EQ(scaled.height, 24);

    const auto normalized = SurfacePainter::measureText("A", -2);
    EXPECT_EQ(normalized.width, 8);
    EXPECT_EQ(normalized.height, 12);

    const auto empty = SurfacePainter::measureText("", 3);
    EXPECT_EQ(empty.width, 0);
    EXPECT_EQ(empty.height, 0);
}

TEST(SurfacePainterTest, MeasuresUtf8TextAfterAsciiSimplification) {
    const auto unsupportedCodepoint = SurfacePainter::measureText("\xC3\xA9");
    EXPECT_EQ(unsupportedCodepoint.width, BitmapFont::GlyphWidth);
    EXPECT_EQ(unsupportedCodepoint.height, BitmapFont::GlyphHeight);

    const auto enDash = SurfacePainter::measureText("\xE2\x80\x93");
    EXPECT_EQ(enDash.width, BitmapFont::GlyphWidth);

    const auto ellipsis = SurfacePainter::measureText("\xE2\x80\xA6");
    EXPECT_EQ(ellipsis.width, BitmapFont::GlyphWidth * 3 + BitmapFont::GlyphGap * 2);

    const auto smartQuoted = SurfacePainter::measureText("\xE2\x80\x9Cok\xE2\x80\x9D");
    EXPECT_EQ(smartQuoted.width, BitmapFont::GlyphWidth * 4 + BitmapFont::GlyphGap * 3);

    const auto truncated = SurfacePainter::measureText(std::string_view("\xE2", 1));
    EXPECT_EQ(truncated.width, BitmapFont::GlyphWidth);
}

TEST(SurfacePainterTest, FillsRectsAndDrawsPointsInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };

    for (const auto format : formats) {
        auto surface = makeSurface(format, 13, 9);
        SurfacePainter painter(format, surface.width, surface.height, planeSpan(surface));
        ASSERT_TRUE(painter.valid()) << static_cast<int>(format);

        painter.fillRect(-3, -2, 8, 6, pek::Colors::red);
        painter.drawPoint(12, 8, pek::Colors::lime, 4);

        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsSegmentationMasksInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeSegmentationFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 16, 12);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, ReplacesBackgroundFromSegmentationMasksInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeSegmentationFrameResults("backgroundReplacement");
    const auto background = makeBackgroundImage();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 16, 12);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;
        request.options.backgroundImage = &background;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(SegmentationMaskTest, BackgroundReplacementBlendsSharedYuvChromaForMixedBlocks) {
    constexpr std::uint8_t InitialY = 80U;
    constexpr std::uint8_t InitialU = 33U;
    constexpr std::uint8_t InitialV = 44U;
    constexpr std::uint8_t ReplacementY = 16U;
    constexpr std::uint8_t ReplacementU = 128U;
    constexpr std::uint8_t ReplacementV = 128U;
    constexpr auto QuarterBlock = 1U;
    constexpr auto HalfPair = 1U;
    constexpr std::array formats{
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };

    for (const auto format : formats) {
        const auto height = format == RawImagePixelFormat::Yuy2 ? 1U : 2U;
        auto surface = makeSurface(format, 4U, height);
        fillActivePlane(surface, 0U, InitialY);
        if (format == RawImagePixelFormat::I420) {
            fillActivePlane(surface, 1U, InitialU);
            fillActivePlane(surface, 2U, InitialV);
        } else if (format == RawImagePixelFormat::Nv12) {
            for (std::uint32_t y = 0; y < surface.storage[1].height; ++y) {
                auto *row = planeRow(surface, 1U, y);
                for (std::uint32_t x = 0; x < surface.storage[1].rowBytes; x += 2U) {
                    row[x] = InitialU;
                    row[x + 1U] = InitialV;
                }
            }
        } else {
            auto *row = planeRow(surface, 0U, 0U);
            for (std::uint32_t x = 0; x < surface.storage[0].rowBytes; x += 4U) {
                row[x] = InitialY;
                row[x + 1U] = InitialU;
                row[x + 2U] = InitialY;
                row[x + 3U] = InitialV;
            }
        }

        const std::vector<std::uint8_t> maskPixels =
            format == RawImagePixelFormat::Yuy2 ? std::vector<std::uint8_t>{255U, 0U, 255U, 255U}
                                                : std::vector<std::uint8_t>{
                                                      255U,
                                                      0U,
                                                      255U,
                                                      255U,
                                                      0U,
                                                      0U,
                                                      255U,
                                                      255U,
                                                  };
        const pek::raster::MaskView mask{
            maskPixels.data(),
            maskPixels.size(),
            4U,
            height,
        };
        const pek::raster::ImageSurfaceView surfaceView{
            format,
            surface.width,
            surface.height,
            planeSpan(surface),
        };
        pek::raster::BackgroundReplacementOptions options;
        options.threshold = 150U;
        options.fallbackColor = pek::colorFromRgbBytes(0U, 0U, 0U);

        ASSERT_TRUE(pek::raster::replaceBackgroundFromMask(surfaceView, mask, options))
            << static_cast<int>(format);

        if (format == RawImagePixelFormat::Yuy2) {
            const auto *row = planeRow(surface, 0U, 0U);
            EXPECT_EQ(row[0], ReplacementY);
            EXPECT_EQ(row[1], blendForCoverage(InitialU, ReplacementU, HalfPair, 2U));
            EXPECT_EQ(row[2], InitialY);
            EXPECT_EQ(row[3], blendForCoverage(InitialV, ReplacementV, HalfPair, 2U));
            EXPECT_EQ(row[4], ReplacementY);
            EXPECT_EQ(row[5], ReplacementU);
            EXPECT_EQ(row[6], ReplacementY);
            EXPECT_EQ(row[7], ReplacementV);
        } else {
            const auto *y0 = planeRow(surface, 0U, 0U);
            const auto *y1 = planeRow(surface, 0U, 1U);
            EXPECT_EQ(y0[0], ReplacementY) << static_cast<int>(format);
            EXPECT_EQ(y0[1], InitialY) << static_cast<int>(format);
            EXPECT_EQ(y0[2], ReplacementY) << static_cast<int>(format);
            EXPECT_EQ(y0[3], ReplacementY) << static_cast<int>(format);
            EXPECT_EQ(y1[0], InitialY) << static_cast<int>(format);
            EXPECT_EQ(y1[1], InitialY) << static_cast<int>(format);
            EXPECT_EQ(y1[2], ReplacementY) << static_cast<int>(format);
            EXPECT_EQ(y1[3], ReplacementY) << static_cast<int>(format);
            if (format == RawImagePixelFormat::I420) {
                const auto *u = planeRow(surface, 1U, 0U);
                const auto *v = planeRow(surface, 2U, 0U);
                EXPECT_EQ(u[0], blendForCoverage(InitialU, ReplacementU, QuarterBlock, 4U));
                EXPECT_EQ(v[0], blendForCoverage(InitialV, ReplacementV, QuarterBlock, 4U));
                EXPECT_EQ(u[1], ReplacementU);
                EXPECT_EQ(v[1], ReplacementV);
            } else {
                const auto *uv = planeRow(surface, 1U, 0U);
                EXPECT_EQ(uv[0], blendForCoverage(InitialU, ReplacementU, QuarterBlock, 4U));
                EXPECT_EQ(uv[1], blendForCoverage(InitialV, ReplacementV, QuarterBlock, 4U));
                EXPECT_EQ(uv[2], ReplacementU);
                EXPECT_EQ(uv[3], ReplacementV);
            }
        }
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsTrackTracesInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeTrackTraceFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 80, 48);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsGenericObjectLabelledBoxesInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeGenericObjectFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 13, 9);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsHumanFaceCirclesAndGazeVectorsInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeHumanFaceAndGazeFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 96, 72);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsCameraContactMarkersInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeCameraContactFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 128, 96);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsPersonClassificationInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makePersonClassificationFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 240, 160);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        waitForPersonClassificationBlinkOn();
        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsClassificationListsLeftAndRightInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makeImageClassificationFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 320, 180);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsPerformanceOverlayInAllSupportedFormats) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    const auto frameResults = makePerformanceOverlayFrameResults();

    for (const auto format : formats) {
        auto surface = makeSurface(format, 180, 48);

        pek::osd::RasterDrawRequest request;
        request.surface.format = format;
        request.surface.width = surface.width;
        request.surface.height = surface.height;
        request.surface.planes = planeSpan(surface);
        request.frameResults = &frameResults;
        request.options.performanceOverlayEnabled = true;

        EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn)
            << static_cast<int>(format);
        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(RasterOsdTest, DrawsPerformanceOverlayAsRectangularBlock) {
    perception::FrameResults frameResults;
    perception::appendPerformanceOverlay(frameResults,
                                         {
                                             "A: 1",
                                             "STD/GENIMGPRE/YOLO-OBJDET:   1.18ms",
                                             "Pipeline:  26.0 FPS",
                                         });
    auto surface = makeSurface(RawImagePixelFormat::Bgra, 360, 80);

    pek::osd::RasterDrawRequest request;
    request.surface.format = surface.format;
    request.surface.width = surface.width;
    request.surface.height = surface.height;
    request.surface.planes = planeSpan(surface);
    request.frameResults = &frameResults;
    request.options.performanceOverlayEnabled = true;

    ASSERT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn);

    std::vector<int> changedRowWidths;
    for (std::uint32_t y = 0; y < surface.height; ++y) {
        const int rowWidth = changedBgraRowWidth(surface, y);
        if (rowWidth > 0) {
            changedRowWidths.push_back(rowWidth);
        }
    }

    ASSERT_FALSE(changedRowWidths.empty());
    const int blockWidth = changedRowWidths.front();
    EXPECT_GT(blockWidth, 200);
    for (const int rowWidth : changedRowWidths) {
        EXPECT_EQ(rowWidth, blockWidth);
    }
    expectPaddingUnchanged(surface);
}

TEST(RasterOsdTest, SkipsPerformanceOverlayWhenDisabled) {
    auto surface = makeSurface(RawImagePixelFormat::Bgra, 180, 48);
    const auto frameResults = makePerformanceOverlayFrameResults();

    pek::osd::RasterDrawRequest request;
    request.surface.format = surface.format;
    request.surface.width = surface.width;
    request.surface.height = surface.height;
    request.surface.planes = planeSpan(surface);
    request.frameResults = &frameResults;
    request.options.performanceOverlayEnabled = false;

    EXPECT_EQ(pek::osd::drawRasterOsd(request), pek::osd::RasterDrawStatus::Drawn);
    EXPECT_FALSE(activeAreaChanged(surface));
    expectPaddingUnchanged(surface);
}

TEST(SurfacePainterTest, DrawsAndClipsAllSupportedFormatsWithoutTouchingPadding) {
    constexpr std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };

    for (const auto format : formats) {
        auto surface = makeSurface(format, 9, 7);
        SurfacePainter painter(format, surface.width, surface.height, planeSpan(surface));
        ASSERT_TRUE(painter.valid()) << static_cast<int>(format);

        painter.drawRect(-1000, -1000, 1004, 1004, pek::Colors::red, 2);
        painter.drawLine(-1000, 3, 1000000, 3, pek::Colors::lime, 2);
        painter.drawCircle(4, 3, 5, pek::Colors::blue, 3);

        EXPECT_TRUE(activeAreaChanged(surface)) << static_cast<int>(format);
        expectPaddingUnchanged(surface);
    }
}

TEST(SurfacePainterTest, TextUsesBackgroundAnchorAndScale) {
    auto surface = makeSurface(RawImagePixelFormat::Bgra, 40, 30);
    SurfacePainter painter(
        RawImagePixelFormat::Bgra, surface.width, surface.height, planeSpan(surface));
    ASSERT_TRUE(painter.valid());

    const auto metrics = SurfacePainter::measureText("A+1", 2);
    EXPECT_EQ(metrics.width, 52);
    EXPECT_EQ(metrics.height, 24);

    painter.drawText(
        39, 29, "A+1", pek::Colors::white, pek::Colors::black, 2, TextAnchor::BottomRight);
    EXPECT_TRUE(activeAreaChanged(surface));
    expectPaddingUnchanged(surface);

    const auto before = surface.storage[0].bytes;
    painter.drawText(0, 0, "", pek::Colors::white, pek::Colors::black);
    EXPECT_EQ(surface.storage[0].bytes, before);
}

TEST(SurfacePainterTest, ThicknessLessThanOneIsNormalized) {
    auto surface = makeSurface(RawImagePixelFormat::Rgb, 10, 10);
    SurfacePainter painter(
        RawImagePixelFormat::Rgb, surface.width, surface.height, planeSpan(surface));
    ASSERT_TRUE(painter.valid());

    painter.drawRect(2, 2, 5, 5, pek::Colors::red, 0);
    painter.drawLine(0, 0, 9, 9, pek::Colors::lime, -5);
    painter.drawCircle(5, 5, 3, pek::Colors::blue, 20);

    EXPECT_TRUE(activeAreaChanged(surface));
    expectPaddingUnchanged(surface);
}

TEST(SurfacePainterTest, CanUseMappedPixelBufferVideoFramePlanes) {
    constexpr std::uint32_t width = 8;
    constexpr std::uint32_t height = 6;
    constexpr std::uint32_t stride = width * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height, Sentinel);
    auto frame = pek::mediaio::makePixelBufferVideoFrame(pixels.data(),
                                                         pixels.size(),
                                                         width,
                                                         height,
                                                         RawImagePixelFormat::Bgra,
                                                         stride,
                                                         pek::AccessMode::ReadWrite);
    ASSERT_NE(frame, nullptr);
    auto mapped = frame->map(pek::AccessMode::ReadWrite);
    ASSERT_NE(mapped, nullptr);

    std::array<ImagePlaneDesc, pek::MaxImagePlaneCount> planes{};
    std::size_t planeCount = 0;
    for (const auto &plane : mapped->planes()) {
        planes[planeCount++] = {
            nullptr,
            static_cast<std::uint8_t *>(plane.mutableData()),
            plane.byteSize(),
            plane.strideBytes(),
        };
    }

    SurfacePainter painter(mapped->format(),
                           mapped->width(),
                           mapped->height(),
                           std::span<ImagePlaneDesc>(planes.data(), planeCount),
                           mapped->yuvColorMatrix(),
                           mapped->yuvRange());
    ASSERT_TRUE(painter.valid());
    painter.drawText(0, 0, "OK", pek::Colors::white, pek::Colors::black);

    EXPECT_NE(std::find_if(pixels.begin(),
                           pixels.end(),
                           [](std::uint8_t value) { return value != Sentinel; }),
              pixels.end());
}
