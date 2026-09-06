/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file RasterOsd.cpp
 * @brief Raster-backed rendering implementation for the PEK OSD element.
 */

#include "RasterOsd.h"

#include "pek/Color.h"
#include "pek/Tools.h"
#include "raster/SegmentationMask.h"
#include "raster/SurfacePainter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace pek::osd {
namespace {

constexpr const char *GenericObjectContentType = "genericObject";
constexpr const char *HumanFaceContentType = "humanFace";
constexpr const char *EyeYawPitchContentType = "eyeYawPitch";
constexpr const char *CameraContactContentType = "cameraContact";
constexpr const char *ClassificationContentType = "classification";
constexpr const char *PersonClassificationContentType = "personClassification";
constexpr const char *SegmentationContentType = "segmentation";
constexpr const char *BottomRightCompositingMode = "bottomRight";
constexpr const char *BackgroundReplacementCompositingMode = "backgroundReplacement";
constexpr int ObjectBoxThickness = 2;
constexpr int HumanFaceCircleThickness = 2;
constexpr pek::Color HumanFaceCircleColor = pek::colorFromRgbBytes(38, 0, 255);
constexpr float GazeVectorLength = 120.0f;
constexpr float GazePoseDeadZoneDegrees = 0.1f;
constexpr int GazeVectorThickness = 2;
constexpr float GazeArrowHeadLength = 12.0f;
constexpr float GazeArrowHeadWidth = 8.0f;
constexpr int CameraContactClassId = 1;
constexpr float CameraContactRadiusScale = 0.65f;
constexpr float CameraContactMinRadius = 18.0f;
constexpr float CameraContactMaxRadius = 80.0f;
constexpr float NoCameraContactRadiusScale = 1.15f;
constexpr float NoCameraContactMinRadius = 28.0f;
constexpr float NoCameraContactMaxRadius = 140.0f;
constexpr int CameraContactCircleThickness = 5;
constexpr int NoCameraContactCircleThickness = 8;
constexpr int CameraContactPointSize = 10;
constexpr int NoCameraContactPointSize = 14;
constexpr pek::Color PersonPresenceColor = pek::colorFromRgbBytes(102, 255, 0);
constexpr pek::Color NoPersonPresenceColor = pek::colorFromRgbBytes(255, 68, 68);
constexpr std::uint32_t PersonTextScaleHeightDivisor = 180U;
constexpr int MaxPersonTextScale = 8;
constexpr std::uint32_t TextScaleHeightDivisor = 540U;
constexpr int MinTextScale = 1;
constexpr int MaxTextScale = 3;
constexpr int ClassificationPadding = 10;
constexpr int ClassificationLineHeightNumerator = 3;
constexpr int ClassificationLineHeightDenominator = 2;
constexpr int PerformanceXOffset = 10;
constexpr int PerformanceYOffset = 10;
constexpr int TrackTraceThickness = 4;
constexpr std::array<pek::Color, 8> TrackTracePalette = {
    pek::Colors::yellow,
    pek::Colors::lime,
    pek::Colors::cyan,
    pek::Colors::magenta,
    pek::Colors::orange,
    pek::Colors::deepSkyBlue,
    pek::Colors::fuchsia,
    pek::Colors::chartreuse,
};

struct PixelBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

bool hasContentType(const perception::metadata::LayerInfoT *layer,
                    const char *contentType) noexcept {
    return layer != nullptr && layer->content_type == contentType;
}

bool isGenericObjectLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, GenericObjectContentType);
}

bool isHumanFaceLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, HumanFaceContentType);
}

bool isEyeYawPitchLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, EyeYawPitchContentType);
}

bool isCameraContactLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, CameraContactContentType);
}

bool isClassificationLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, ClassificationContentType);
}

bool isPersonClassificationLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, PersonClassificationContentType);
}

bool isSegmentationLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, SegmentationContentType);
}

bool isBottomRightLayer(const perception::metadata::LayerInfoT *layer) noexcept {
    return layer != nullptr && layer->compositing_mode == BottomRightCompositingMode;
}

bool usesBackgroundReplacement(const perception::metadata::LayerInfoT *layer) noexcept {
    return layer != nullptr && layer->compositing_mode == BackgroundReplacementCompositingMode;
}

int textScaleForHeight(std::uint32_t height) noexcept {
    return std::clamp(
        static_cast<int>(height / TextScaleHeightDivisor), MinTextScale, MaxTextScale);
}

int personTextScaleForHeight(std::uint32_t height) noexcept {
    return std::clamp(
        static_cast<int>(height / PersonTextScaleHeightDivisor), MinTextScale, MaxPersonTextScale);
}

int surfaceDimensionToInt(std::uint32_t value) noexcept {
    return static_cast<int>(std::min<std::uint32_t>(
        value, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

pek::Color colorForTrack(std::uint64_t trackId) noexcept {
    return TrackTracePalette[trackId % TrackTracePalette.size()];
}

pek::raster::ImageSurfaceView makeImageSurfaceView(const RasterSurface &surface) noexcept {
    return {
        .format = surface.format,
        .width = surface.width,
        .height = surface.height,
        .planes = surface.planes,
        .yuvMatrix = surface.yuvMatrix,
        .yuvRange = surface.yuvRange,
    };
}

pek::raster::MaskView makeMaskView(const perception::metadata::BitmapDataT &bitmap) noexcept {
    return {
        .data = bitmap.pixels.data(),
        .size = bitmap.pixels.size(),
        .width = bitmap.width,
        .height = bitmap.height,
    };
}

int textLineHeight(int scale) noexcept {
    const auto height = pek::raster::SurfacePainter::measureText("M", scale).height;
    return height > 0 ? static_cast<int>(height) : 1;
}

int classificationLineHeight(int scale) noexcept {
    return std::max(1,
                    (textLineHeight(scale) * ClassificationLineHeightNumerator) /
                        ClassificationLineHeightDenominator);
}

float deg2rad(float degrees) noexcept {
    return degrees * 3.1415926535f / 180.0f;
}

int roundToInt(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }

    const double coordinate = static_cast<double>(value);
    if (coordinate <= static_cast<double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    if (coordinate >= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(std::lround(coordinate));
}

int clampRasterEdge(double value, std::uint32_t upper) noexcept {
    const double maxCoordinate =
        std::min(static_cast<double>(upper), static_cast<double>(std::numeric_limits<int>::max()));

    if (std::isnan(value) || value <= 0.0) {
        return 0;
    }
    if (value >= maxCoordinate) {
        return static_cast<int>(maxCoordinate);
    }
    return static_cast<int>(std::lround(value));
}

bool makePixelBox(const RasterSurface &surface,
                  const perception::metadata::BoundingBoxT &box,
                  PixelBox &out) noexcept {
    if (surface.width == 0U || surface.height == 0U || !std::isfinite(box.x) ||
        !std::isfinite(box.y) || !std::isfinite(box.width) || !std::isfinite(box.height) ||
        box.width <= 0.0f || box.height <= 0.0f) {
        return false;
    }

    const int x0 = clampRasterEdge(static_cast<double>(box.x), surface.width);
    const int y0 = clampRasterEdge(static_cast<double>(box.y), surface.height);
    const int x1 = clampRasterEdge(static_cast<double>(box.x) + box.width, surface.width);
    const int y1 = clampRasterEdge(static_cast<double>(box.y) + box.height, surface.height);
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }

    out = {x0, y0, x1 - x0, y1 - y0};
    return true;
}

void gazeEndpoint(float eyeX,
                  float eyeY,
                  float yawDeg,
                  float pitchDeg,
                  float lengthPx,
                  float &outX,
                  float &outY) noexcept {
    const float yaw = deg2rad(yawDeg);
    const float pitch = deg2rad(pitchDeg);

    float dx = -std::tan(yaw);
    float dy = -std::tan(pitch);

    const float n = std::sqrt(dx * dx + dy * dy);
    if (n <= 0.0f || !std::isfinite(n)) {
        outX = eyeX;
        outY = eyeY;
        return;
    }

    dx /= n;
    dy /= n;

    outX = eyeX + dx * lengthPx;
    outY = eyeY + dy * lengthPx;
}

void drawSimpleArrow(pek::raster::SurfacePainter &painter,
                     float fromX,
                     float fromY,
                     float toX,
                     float toY,
                     pek::Color color) noexcept {
    if (!std::isfinite(fromX) || !std::isfinite(fromY) || !std::isfinite(toX) ||
        !std::isfinite(toY)) {
        return;
    }

    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f || !std::isfinite(len)) {
        painter.drawPoint(roundToInt(fromX), roundToInt(fromY), color, GazeVectorThickness);
        return;
    }

    const float ux = dx / len;
    const float uy = dy / len;
    const float headLength = std::min(GazeArrowHeadLength, len);
    const float headHalfWidth = GazeArrowHeadWidth * 0.5f;
    const float baseX = toX - ux * headLength;
    const float baseY = toY - uy * headLength;
    const float px = -uy;
    const float py = ux;

    painter.drawLine(roundToInt(fromX),
                     roundToInt(fromY),
                     roundToInt(toX),
                     roundToInt(toY),
                     color,
                     GazeVectorThickness);
    painter.drawLine(roundToInt(toX),
                     roundToInt(toY),
                     roundToInt(baseX + px * headHalfWidth),
                     roundToInt(baseY + py * headHalfWidth),
                     color,
                     GazeVectorThickness);
    painter.drawLine(roundToInt(toX),
                     roundToInt(toY),
                     roundToInt(baseX - px * headHalfWidth),
                     roundToInt(baseY - py * headHalfWidth),
                     color,
                     GazeVectorThickness);
}

void drawSegmentationMasks(pek::raster::ImageSurfaceView surface,
                           const perception::FrameResults &frameResults,
                           const RasterDrawOptions &options) {
    frameResults.for_each<perception::metadata::SegmentationMasksT>(
        [surface, &options](const auto &payload) {
            if (!isSegmentationLayer(payload.layer.get())) {
                return;
            }

            const bool replaceBackground = usesBackgroundReplacement(payload.layer.get());
            for (const auto &mask : payload.masks) {
                if (!mask || !mask->bitmap) {
                    continue;
                }

                const auto maskView = makeMaskView(*mask->bitmap);
                if (replaceBackground) {
                    (void)pek::raster::replaceBackgroundFromMask(
                        surface,
                        maskView,
                        pek::raster::BackgroundReplacementOptions{
                            .backgroundImage = options.backgroundImage,
                        });
                } else {
                    (void)pek::raster::blendSegmentationMask(surface, maskView);
                }
            }
        });
}

bool isFinitePoint(const perception::metadata::Point2fT &point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

void drawTrackTrace(pek::raster::SurfacePainter &painter,
                    const perception::metadata::TrackTraceT &trace) noexcept {
    if (trace.points.size() < 2U) {
        return;
    }

    const pek::Color color = colorForTrack(trace.track_id);
    bool hasPrevious = false;
    int previousX = 0;
    int previousY = 0;
    for (const auto &point : trace.points) {
        if (!point || !isFinitePoint(*point)) {
            hasPrevious = false;
            continue;
        }

        const int x = roundToInt(point->x);
        const int y = roundToInt(point->y);
        if (hasPrevious) {
            painter.drawLine(previousX, previousY, x, y, color, TrackTraceThickness);
        }

        previousX = x;
        previousY = y;
        hasPrevious = true;
    }
}

void drawTrackTraces(pek::raster::SurfacePainter &painter,
                     const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::TrackTracesT>([&painter](const auto &payload) {
        for (const auto &trace : payload.traces) {
            if (trace) {
                drawTrackTrace(painter, *trace);
            }
        }
    });
}

std::set<std::uint64_t> collectTrackedSourceIds(const perception::FrameResults &frameResults,
                                                const char *contentType) {
    std::set<std::uint64_t> result;

    frameResults.for_each<perception::metadata::ObjectTracksT>(
        [&result, contentType](const auto &payload) {
            if (!hasContentType(payload.layer.get(), contentType)) {
                return;
            }

            for (const auto &track : payload.tracks) {
                if (track && track->source_id != 0U) {
                    result.insert(track->source_id);
                }
            }
        });

    return result;
}

bool isTrackedSourceDetection(const perception::metadata::BoxDetectionT &detection,
                              const std::set<std::uint64_t> &trackedSourceIds) {
    return detection.object != nullptr && trackedSourceIds.contains(detection.object->id);
}

const perception::metadata::BoxDetectionT *
findFirstHumanFaceDetection(const perception::FrameResults &frameResults, std::uint64_t id) {
    if (id == 0U) {
        return nullptr;
    }

    const perception::metadata::BoxDetectionT *parent = nullptr;

    frameResults.for_each<perception::metadata::BoxDetectionsT>([&parent, id](const auto &payload) {
        if (parent != nullptr || !isHumanFaceLayer(payload.layer.get())) {
            return;
        }

        for (const auto &detection : payload.detections) {
            if (!detection || !detection->object || !detection->box) {
                continue;
            }
            if (detection->object->id != id) {
                continue;
            }

            parent = detection.get();
            return;
        }
    });

    return parent;
}

const perception::metadata::BoxDetectionT *
findOnlyHumanFaceDetection(const perception::FrameResults &frameResults, std::uint64_t id) {
    if (id == 0U) {
        return nullptr;
    }

    const perception::metadata::BoxDetectionT *parent = nullptr;
    std::size_t parentCount = 0U;

    frameResults.for_each<perception::metadata::BoxDetectionsT>(
        [&parent, &parentCount, id](const auto &payload) {
            if (!isHumanFaceLayer(payload.layer.get())) {
                return;
            }

            for (const auto &detection : payload.detections) {
                if (!detection || !detection->object || !detection->box) {
                    continue;
                }
                if (detection->object->id != id) {
                    continue;
                }

                parent = detection.get();
                ++parentCount;
            }
        });

    return parentCount == 1U ? parent : nullptr;
}

void drawHumanFaceCircle(pek::raster::SurfacePainter &painter,
                         const RasterSurface &surface,
                         const perception::metadata::BoundingBoxT &box) noexcept {
    PixelBox pixelBox;
    if (!makePixelBox(surface, box, pixelBox)) {
        return;
    }

    const int cx = pixelBox.x + pixelBox.width / 2;
    const int cy = pixelBox.y + pixelBox.height / 2;
    const int radius = std::max(1, pixelBox.width / 2);
    painter.drawCircle(cx, cy, radius, HumanFaceCircleColor, HumanFaceCircleThickness);
}

void drawHumanFaceDetections(pek::raster::SurfacePainter &painter,
                             const RasterSurface &surface,
                             const perception::FrameResults &frameResults,
                             const std::set<std::uint64_t> &trackedSourceIds) {
    frameResults.for_each<perception::metadata::BoxDetectionsT>(
        [&painter, &surface, &trackedSourceIds](const auto &payload) {
            if (!isHumanFaceLayer(payload.layer.get())) {
                return;
            }

            for (const auto &detection : payload.detections) {
                if (!detection || !detection->box ||
                    isTrackedSourceDetection(*detection, trackedSourceIds)) {
                    continue;
                }

                drawHumanFaceCircle(painter, surface, *detection->box);
            }
        });
}

void drawHumanFaceTracks(pek::raster::SurfacePainter &painter,
                         const RasterSurface &surface,
                         const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ObjectTracksT>(
        [&painter, &surface](const auto &payload) {
            if (!isHumanFaceLayer(payload.layer.get())) {
                return;
            }

            for (const auto &track : payload.tracks) {
                if (!track || !track->box) {
                    continue;
                }

                drawHumanFaceCircle(painter, surface, *track->box);
            }
        });
}

void drawHumanFaces(pek::raster::SurfacePainter &painter,
                    const RasterSurface &surface,
                    const perception::FrameResults &frameResults) {
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults, HumanFaceContentType);
    drawHumanFaceDetections(painter, surface, frameResults, trackedSourceIds);
    drawHumanFaceTracks(painter, surface, frameResults);
}

void drawLabelledBox(pek::raster::SurfacePainter &painter,
                     const RasterSurface &surface,
                     const perception::metadata::BoundingBoxT &box,
                     std::string_view label) noexcept {
    PixelBox pixelBox;
    if (!makePixelBox(surface, box, pixelBox)) {
        return;
    }

    if (!label.empty()) {
        const int textScale = textScaleForHeight(surface.height);
        painter.drawText(
            pixelBox.x, pixelBox.y, label, pek::Colors::white, pek::Colors::black, textScale);
    }
    painter.drawRect(pixelBox.x,
                     pixelBox.y,
                     pixelBox.width,
                     pixelBox.height,
                     pek::Colors::red,
                     ObjectBoxThickness);
}

void drawGenericObjectDetections(pek::raster::SurfacePainter &painter,
                                 const RasterSurface &surface,
                                 const perception::FrameResults &frameResults,
                                 const std::set<std::uint64_t> &trackedSourceIds) {
    frameResults.for_each<perception::metadata::BoxDetectionsT>(
        [&painter, &surface, &trackedSourceIds](const auto &payload) {
            if (!isGenericObjectLayer(payload.layer.get())) {
                return;
            }

            for (const auto &detection : payload.detections) {
                if (!detection || !detection->box ||
                    isTrackedSourceDetection(*detection, trackedSourceIds)) {
                    continue;
                }

                drawLabelledBox(painter, surface, *detection->box, detection->text);
            }
        });
}

void drawGenericObjectTracks(pek::raster::SurfacePainter &painter,
                             const RasterSurface &surface,
                             const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ObjectTracksT>(
        [&painter, &surface](const auto &payload) {
            if (!isGenericObjectLayer(payload.layer.get())) {
                return;
            }

            for (const auto &track : payload.tracks) {
                if (!track || !track->box) {
                    continue;
                }

                drawLabelledBox(painter, surface, *track->box, track->text);
            }
        });
}

void drawLabelledBoxes(pek::raster::SurfacePainter &painter,
                       const RasterSurface &surface,
                       const perception::FrameResults &frameResults) {
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults, GenericObjectContentType);
    drawGenericObjectDetections(painter, surface, frameResults, trackedSourceIds);
    drawGenericObjectTracks(painter, surface, frameResults);
}

void drawGazeVector(pek::raster::SurfacePainter &painter,
                    const RasterSurface &surface,
                    const perception::FrameResults &frameResults,
                    const perception::metadata::PoseEstimationT &pose) {
    if (!pose.object || !std::isfinite(pose.yaw) || !std::isfinite(pose.pitch)) {
        return;
    }

    const auto *parent = findOnlyHumanFaceDetection(frameResults, pose.object->parent_id);
    if (parent == nullptr || !parent->box) {
        return;
    }

    PixelBox parentBox;
    if (!makePixelBox(surface, *parent->box, parentBox)) {
        return;
    }

    const float x = static_cast<float>(parentBox.x) + static_cast<float>(parentBox.width) / 2.0f;
    const float y = static_cast<float>(parentBox.y) + static_cast<float>(parentBox.height) / 2.0f;
    if (std::abs(pose.yaw) < GazePoseDeadZoneDegrees &&
        std::abs(pose.pitch) < GazePoseDeadZoneDegrees) {
        return;
    }

    float xEnd = x;
    float yEnd = y;
    gazeEndpoint(x, y, pose.yaw, pose.pitch, GazeVectorLength, xEnd, yEnd);
    drawSimpleArrow(painter, x, y, xEnd, yEnd, pek::Colors::lightGoldenrodYellow);
}

void drawGazeVectors(pek::raster::SurfacePainter &painter,
                     const RasterSurface &surface,
                     const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::PoseEstimationsT>(
        [&painter, &surface, &frameResults](const auto &payload) {
            if (!isEyeYawPitchLayer(payload.layer.get())) {
                return;
            }

            for (const auto &pose : payload.poses) {
                if (pose) {
                    drawGazeVector(painter, surface, frameResults, *pose);
                }
            }
        });
}

void drawCameraContactMarker(pek::raster::SurfacePainter &painter,
                             const RasterSurface &surface,
                             const perception::FrameResults &frameResults,
                             const perception::metadata::ClassificationT &classification) {
    if (!classification.object || classification.candidates.empty() ||
        !classification.candidates.front()) {
        return;
    }

    const auto &candidate = *classification.candidates.front();
    if (candidate.class_id < 0) {
        return;
    }

    const auto *parent =
        findFirstHumanFaceDetection(frameResults, classification.object->parent_id);
    if (parent == nullptr || !parent->box) {
        return;
    }

    PixelBox faceBox;
    if (!makePixelBox(surface, *parent->box, faceBox)) {
        return;
    }

    const bool hasCameraContact = candidate.class_id == CameraContactClassId;
    const pek::Color color = hasCameraContact ? pek::Colors::lime : pek::Colors::red;
    const float baseRadius = static_cast<float>(std::min(faceBox.width, faceBox.height)) * 0.5f;
    const float radius = hasCameraContact ? std::clamp(baseRadius * CameraContactRadiusScale,
                                                       CameraContactMinRadius,
                                                       CameraContactMaxRadius)
                                          : std::clamp(baseRadius * NoCameraContactRadiusScale,
                                                       NoCameraContactMinRadius,
                                                       NoCameraContactMaxRadius);
    const int thickness =
        hasCameraContact ? CameraContactCircleThickness : NoCameraContactCircleThickness;
    const int pointSize = hasCameraContact ? CameraContactPointSize : NoCameraContactPointSize;
    const int cx = faceBox.x + faceBox.width / 2;
    const int cy = faceBox.y + faceBox.height / 2;

    painter.drawCircle(cx, cy, std::max(1, roundToInt(radius)), color, thickness);
    painter.drawPoint(cx, cy, color, pointSize);
}

void drawCameraContactMarkers(pek::raster::SurfacePainter &painter,
                              const RasterSurface &surface,
                              const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ClassificationsT>(
        [&painter, &surface, &frameResults](const auto &payload) {
            if (!isCameraContactLayer(payload.layer.get())) {
                return;
            }

            for (const auto &classification : payload.classifications) {
                if (classification) {
                    drawCameraContactMarker(painter, surface, frameResults, *classification);
                }
            }
        });
}

void drawPersonPresence(pek::raster::SurfacePainter &painter,
                        const RasterSurface &surface,
                        const perception::metadata::PersonPresenceT &presence) {
    if (surface.width == 0U || surface.height == 0U || pek::Time::utcMs() % 1000U >= 800U) {
        return;
    }

    const bool isPerson = presence.yes_confidence > presence.no_confidence;
    const std::string_view label = isPerson ? "PERSON" : "NON-PERSON";
    const pek::Color color = isPerson ? PersonPresenceColor : NoPersonPresenceColor;
    const int scale = personTextScaleForHeight(surface.height);
    const int centerX = static_cast<int>(
        std::min<std::uint32_t>(surface.width / 2U, std::numeric_limits<int>::max()));
    const int centerY = static_cast<int>(
        std::min<std::uint32_t>(surface.height / 2U, std::numeric_limits<int>::max()));

    painter.drawText(
        centerX, centerY, label, color, pek::Colors::black, scale, pek::raster::TextAnchor::Center);
}

void drawPersonClassifications(pek::raster::SurfacePainter &painter,
                               const RasterSurface &surface,
                               const perception::FrameResults &frameResults) {
    frameResults.for_each<perception::metadata::ClassificationsT>(
        [&painter, &surface](const auto &payload) {
            if (!isPersonClassificationLayer(payload.layer.get())) {
                return;
            }

            for (const auto &presence : payload.person_presence) {
                if (presence) {
                    drawPersonPresence(painter, surface, *presence);
                }
            }
        });
}

int drawClassificationList(pek::raster::SurfacePainter &painter,
                           const RasterSurface &surface,
                           bool alignRight,
                           int bottomOffset,
                           std::string_view heading,
                           const perception::metadata::ClassificationT &classification) {
    const int scale = textScaleForHeight(surface.height);
    const int lineHeight = classificationLineHeight(scale);
    const int width = surfaceDimensionToInt(surface.width);
    const int height = surfaceDimensionToInt(surface.height);
    const auto numResults = static_cast<int>(classification.candidates.size());
    const int lineX = alignRight ? std::max(ClassificationPadding, width - ClassificationPadding)
                                 : ClassificationPadding;
    const int startY =
        height - bottomOffset - (numResults * lineHeight) - (2 * ClassificationPadding);
    const auto anchor =
        alignRight ? pek::raster::TextAnchor::TopRight : pek::raster::TextAnchor::TopLeft;

    if (!heading.empty()) {
        painter.drawText(lineX,
                         std::max(ClassificationPadding, startY - lineHeight),
                         heading,
                         pek::Colors::white,
                         pek::Colors::black,
                         scale,
                         anchor);
    }

    for (int i = 0; i < numResults; ++i) {
        const auto &candidate = classification.candidates[static_cast<std::size_t>(i)];
        if (!candidate) {
            continue;
        }

        const auto text = std::format(
            "#{}: {} ({:.1f}%)", i + 1, candidate->text, candidate->confidence * 100.0f);
        painter.drawText(lineX,
                         startY + (i * lineHeight),
                         text,
                         pek::Colors::white,
                         pek::Colors::black,
                         scale,
                         anchor);
    }

    return (numResults * lineHeight) + (heading.empty() ? 0 : lineHeight) +
           (2 * ClassificationPadding);
}

void drawImageClassifications(pek::raster::SurfacePainter &painter,
                              const RasterSurface &surface,
                              const perception::FrameResults &frameResults) {
    int leftBottomOffset = 0;
    int rightBottomOffset = 0;
    frameResults.for_each<perception::metadata::ClassificationsT>(
        [&painter, &surface, &leftBottomOffset, &rightBottomOffset](const auto &payload) {
            if (!isClassificationLayer(payload.layer.get())) {
                return;
            }

            const bool alignRight = isBottomRightLayer(payload.layer.get());
            const std::string_view heading =
                payload.layer != nullptr && payload.layer->producer != nullptr
                    ? std::string_view(payload.layer->producer->implementation)
                    : std::string_view{};
            int &bottomOffset = alignRight ? rightBottomOffset : leftBottomOffset;
            for (const auto &classification : payload.classifications) {
                if (classification) {
                    bottomOffset += drawClassificationList(
                        painter, surface, alignRight, bottomOffset, heading, *classification);
                }
            }
        });
}

void drawPerformanceOverlay(pek::raster::SurfacePainter &painter,
                            const RasterSurface &surface,
                            const perception::FrameResults &frameResults) {
    if (surface.height == 0U) {
        return;
    }

    int lineY = PerformanceYOffset;
    const int textScale = textScaleForHeight(surface.height);
    const int lineHeight = textLineHeight(textScale);
    const int height =
        static_cast<int>(std::min<std::uint32_t>(surface.height, std::numeric_limits<int>::max()));
    frameResults.for_each<perception::metadata::PerformanceOverlayT>(
        [&painter, height, textScale, lineHeight, &lineY](const auto &payload) {
            for (const auto &line : payload.lines) {
                if (lineY >= height) {
                    return;
                }

                painter.drawText(PerformanceXOffset,
                                 lineY,
                                 line,
                                 pek::Colors::lime,
                                 pek::Colors::black,
                                 textScale);
                lineY += lineHeight;
            }
        });
}

} // namespace

bool supportsRasterSurfaceFormat(pek::RawImagePixelFormat format) noexcept {
    using enum pek::RawImagePixelFormat;

    switch (format) {
    case Bgra:
    case Rgb:
    case I420:
    case Nv12:
    case Yuy2:
        return true;
    default:
        return false;
    }
}

RasterDrawStatus drawRasterOsd(const RasterDrawRequest &request) noexcept {
    if (request.frameResults == nullptr) {
        return RasterDrawStatus::MissingFrameResults;
    }
    if (!supportsRasterSurfaceFormat(request.surface.format)) {
        return RasterDrawStatus::UnsupportedFormat;
    }

    pek::raster::SurfacePainter painter(request.surface.format,
                                        request.surface.width,
                                        request.surface.height,
                                        request.surface.planes,
                                        request.surface.yuvMatrix,
                                        request.surface.yuvRange);
    if (!painter.valid()) {
        return RasterDrawStatus::InvalidSurface;
    }

    drawSegmentationMasks(
        makeImageSurfaceView(request.surface), *request.frameResults, request.options);
    drawTrackTraces(painter, *request.frameResults);
    drawHumanFaces(painter, request.surface, *request.frameResults);
    drawLabelledBoxes(painter, request.surface, *request.frameResults);
    drawGazeVectors(painter, request.surface, *request.frameResults);
    drawCameraContactMarkers(painter, request.surface, *request.frameResults);
    drawPersonClassifications(painter, request.surface, *request.frameResults);
    drawImageClassifications(painter, request.surface, *request.frameResults);
    if (request.options.performanceOverlayEnabled) {
        drawPerformanceOverlay(painter, request.surface, *request.frameResults);
    }

    return RasterDrawStatus::Drawn;
}

} // namespace pek::osd
