/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file DebugOverlayRenderer.cpp
 * @brief Debug decoration rendering implementation for the OPK OSD element.
 */

#include "DebugOverlayRenderer.h"

#include "opk/Color.h"
#include "opk/Tools.h"
#include "raster/BitmapFont.h"
#include "raster/SegmentationMask.h"
#include "raster/SurfacePainter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace opk::osd {
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
constexpr opk::Color HumanFaceCircleColor = opk::colorFromRgbBytes(38, 0, 255);
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
constexpr opk::Color PersonPresenceColor = opk::colorFromRgbBytes(102, 255, 0);
constexpr opk::Color NoPersonPresenceColor = opk::colorFromRgbBytes(255, 68, 68);
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
constexpr std::array<opk::Color, 8> TrackTracePalette = {
    opk::Colors::yellow,
    opk::Colors::lime,
    opk::Colors::cyan,
    opk::Colors::magenta,
    opk::Colors::orange,
    opk::Colors::deepSkyBlue,
    opk::Colors::fuchsia,
    opk::Colors::chartreuse,
};

struct PixelBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct PerformanceLineParts {
    std::string_view label;
    std::string_view value;
    bool hasSeparator = false;
};

bool hasContentType(const open_perception_kit::metadata::LayerInfoT *layer,
                    const char *contentType) noexcept {
    return layer != nullptr && layer->content_type == contentType;
}

bool isGenericObjectLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, GenericObjectContentType);
}

bool isHumanFaceLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, HumanFaceContentType);
}

bool isEyeYawPitchLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, EyeYawPitchContentType);
}

bool isCameraContactLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, CameraContactContentType);
}

bool isClassificationLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, ClassificationContentType);
}

bool isPersonClassificationLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, PersonClassificationContentType);
}

bool isSegmentationLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return hasContentType(layer, SegmentationContentType);
}

bool isBottomRightLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return layer != nullptr && layer->compositing_mode == BottomRightCompositingMode;
}

bool usesBackgroundReplacement(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
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

opk::Color colorForTrack(std::uint64_t trackId) noexcept {
    return TrackTracePalette[trackId % TrackTracePalette.size()];
}

opk::raster::ImageSurfaceView makeImageSurfaceView(const DebugOverlaySurface &surface) noexcept {
    return {
        .format = surface.format,
        .width = surface.width,
        .height = surface.height,
        .planes = surface.planes,
        .yuvMatrix = surface.yuvMatrix,
        .yuvRange = surface.yuvRange,
    };
}

opk::raster::MaskView
makeMaskView(const open_perception_kit::metadata::BitmapDataT &bitmap) noexcept {
    return {
        .data = bitmap.pixels.data(),
        .size = bitmap.pixels.size(),
        .width = bitmap.width,
        .height = bitmap.height,
    };
}

int textLineHeight(int scale) noexcept {
    const auto height = opk::raster::SurfacePainter::measureText("M", scale).height;
    return height > 0 ? static_cast<int>(height) : 1;
}

std::size_t textGlyphCount(std::string_view text) noexcept {
    const auto metrics = opk::raster::SurfacePainter::measureText(text);
    if (metrics.width <= 0) {
        return 0;
    }

    constexpr auto glyphAdvance =
        opk::raster::BitmapFont::GlyphWidth + opk::raster::BitmapFont::GlyphGap;
    return static_cast<std::size_t>((metrics.width + opk::raster::BitmapFont::GlyphGap) /
                                    glyphAdvance);
}

std::string_view trimTrailingSpaces(std::string_view text) noexcept {
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text;
}

PerformanceLineParts parsePerformanceLine(std::string_view line) noexcept {
    const auto separator = line.find(':');
    if (separator == std::string_view::npos) {
        return {
            .label = trimTrailingSpaces(line),
            .value = {},
            .hasSeparator = false,
        };
    }

    return {
        .label = trimTrailingSpaces(line.substr(0, separator)),
        .value = line.substr(separator + 1),
        .hasSeparator = true,
    };
}

std::vector<std::string> alignPerformanceLines(const std::vector<std::string> &lines) {
    std::vector<PerformanceLineParts> parts;
    parts.reserve(lines.size());

    std::size_t maxLabelGlyphs = 0;
    for (const auto &line : lines) {
        auto parsed = parsePerformanceLine(line);
        if (parsed.hasSeparator) {
            maxLabelGlyphs = std::max(maxLabelGlyphs, textGlyphCount(parsed.label));
        }
        parts.push_back(parsed);
    }

    std::vector<std::string> aligned;
    aligned.reserve(parts.size());

    std::size_t maxLineGlyphs = 0;
    for (const auto &part : parts) {
        std::string line(part.label);
        if (part.hasSeparator) {
            const auto labelGlyphs = textGlyphCount(part.label);
            line.append(maxLabelGlyphs - std::min(maxLabelGlyphs, labelGlyphs), ' ');
            line.push_back(':');
            line.append(part.value);
        }

        maxLineGlyphs = std::max(maxLineGlyphs, textGlyphCount(line));
        aligned.push_back(std::move(line));
    }

    for (auto &line : aligned) {
        const auto lineGlyphs = textGlyphCount(line);
        line.append(maxLineGlyphs - std::min(maxLineGlyphs, lineGlyphs), ' ');
    }
    return aligned;
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

    const auto coordinate = static_cast<double>(value);
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

bool makePixelBox(const DebugOverlaySurface &surface,
                  const open_perception_kit::metadata::BoundingBoxT &box,
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

    outX = eyeX - std::sin(yaw) * std::cos(pitch) * lengthPx;
    outY = eyeY - std::sin(pitch) * lengthPx;
}

void drawSimpleArrow(opk::raster::SurfacePainter &painter,
                     float fromX,
                     float fromY,
                     float toX,
                     float toY,
                     opk::Color color) noexcept {
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

void drawSegmentationMasks(const opk::raster::ImageSurfaceView &surface,
                           const open_perception_kit::FrameResults &frameResults,
                           const DebugOverlayOptions &options) {
    frameResults.for_each<open_perception_kit::metadata::SegmentationMasksT>(
        [&](const auto &payload) { // NOSONAR: payload handling stays local to traversal.
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
                    (void)opk::raster::replaceBackgroundFromMask(
                        surface,
                        maskView,
                        opk::raster::BackgroundReplacementOptions{
                            .backgroundImage = options.backgroundImage,
                        });
                } else {
                    (void)opk::raster::blendSegmentationMask(surface, maskView);
                }
            }
        });
}

bool isFinitePoint(const open_perception_kit::metadata::Point2fT &point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

void drawTrackTrace(opk::raster::SurfacePainter &painter,
                    const open_perception_kit::metadata::TrackTraceT &trace) noexcept {
    if (trace.points.size() < 2U) {
        return;
    }

    const opk::Color color = colorForTrack(trace.track_id);
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

void drawTrackTraces(opk::raster::SurfacePainter &painter,
                     const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::TrackTracesT>(
        [&painter](const auto &payload) {
            for (const auto &trace : payload.traces) {
                if (trace) {
                    drawTrackTrace(painter, *trace);
                }
            }
        });
}

std::set<std::uint64_t>
collectTrackedSourceIds(const open_perception_kit::FrameResults &frameResults,
                        const char *contentType) {
    std::set<std::uint64_t> result;

    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
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

bool isTrackedSourceDetection(const open_perception_kit::metadata::BoxDetectionT &detection,
                              const std::set<std::uint64_t> &trackedSourceIds) {
    return detection.object != nullptr && trackedSourceIds.contains(detection.object->id);
}

const open_perception_kit::metadata::BoxDetectionT *
findFirstHumanFaceDetection(const open_perception_kit::FrameResults &frameResults,
                            std::uint64_t id) {
    if (id == 0U) {
        return nullptr;
    }

    const open_perception_kit::metadata::BoxDetectionT *parent = nullptr;

    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
        [&parent, id](const auto &payload) {
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

const open_perception_kit::metadata::BoxDetectionT *
findOnlyHumanFaceDetection(const open_perception_kit::FrameResults &frameResults,
                           std::uint64_t id) {
    if (id == 0U) {
        return nullptr;
    }

    const open_perception_kit::metadata::BoxDetectionT *parent = nullptr;
    std::size_t parentCount = 0U;

    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
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

void drawHumanFaceCircle(opk::raster::SurfacePainter &painter,
                         const DebugOverlaySurface &surface,
                         const open_perception_kit::metadata::BoundingBoxT &box) noexcept {
    PixelBox pixelBox;
    if (!makePixelBox(surface, box, pixelBox)) {
        return;
    }

    const int cx = pixelBox.x + pixelBox.width / 2;
    const int cy = pixelBox.y + pixelBox.height / 2;
    const int radius = std::max(1, pixelBox.width / 2);
    painter.drawCircle(cx, cy, radius, HumanFaceCircleColor, HumanFaceCircleThickness);
}

void drawHumanFaceDetections(opk::raster::SurfacePainter &painter,
                             const DebugOverlaySurface &surface,
                             const open_perception_kit::FrameResults &frameResults,
                             const std::set<std::uint64_t> &trackedSourceIds) {
    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
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

void drawHumanFaceTracks(opk::raster::SurfacePainter &painter,
                         const DebugOverlaySurface &surface,
                         const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
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

void drawHumanFaces(opk::raster::SurfacePainter &painter,
                    const DebugOverlaySurface &surface,
                    const open_perception_kit::FrameResults &frameResults) {
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults, HumanFaceContentType);
    drawHumanFaceDetections(painter, surface, frameResults, trackedSourceIds);
    drawHumanFaceTracks(painter, surface, frameResults);
}

void drawLabelledBox(opk::raster::SurfacePainter &painter,
                     const DebugOverlaySurface &surface,
                     const open_perception_kit::metadata::BoundingBoxT &box,
                     std::string_view label) noexcept {
    PixelBox pixelBox;
    if (!makePixelBox(surface, box, pixelBox)) {
        return;
    }

    if (!label.empty()) {
        const int textScale = textScaleForHeight(surface.height);
        painter.drawText(
            pixelBox.x, pixelBox.y, label, opk::Colors::white, opk::Colors::black, textScale);
    }
    painter.drawRect(pixelBox.x,
                     pixelBox.y,
                     pixelBox.width,
                     pixelBox.height,
                     opk::Colors::red,
                     ObjectBoxThickness);
}

void drawGenericObjectDetections(opk::raster::SurfacePainter &painter,
                                 const DebugOverlaySurface &surface,
                                 const open_perception_kit::FrameResults &frameResults,
                                 const std::set<std::uint64_t> &trackedSourceIds) {
    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
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

void drawGenericObjectTracks(opk::raster::SurfacePainter &painter,
                             const DebugOverlaySurface &surface,
                             const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
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

void drawLabelledBoxes(opk::raster::SurfacePainter &painter,
                       const DebugOverlaySurface &surface,
                       const open_perception_kit::FrameResults &frameResults) {
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults, GenericObjectContentType);
    drawGenericObjectDetections(painter, surface, frameResults, trackedSourceIds);
    drawGenericObjectTracks(painter, surface, frameResults);
}

void drawGazeVector(opk::raster::SurfacePainter &painter,
                    const DebugOverlaySurface &surface,
                    const open_perception_kit::FrameResults &frameResults,
                    const open_perception_kit::metadata::PoseEstimationT &pose) {
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
    drawSimpleArrow(painter, x, y, xEnd, yEnd, opk::Colors::lightGoldenrodYellow);
}

void drawGazeVectors(opk::raster::SurfacePainter &painter,
                     const DebugOverlaySurface &surface,
                     const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::PoseEstimationsT>(
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

void drawCameraContactMarker(opk::raster::SurfacePainter &painter,
                             const DebugOverlaySurface &surface,
                             const open_perception_kit::FrameResults &frameResults,
                             const open_perception_kit::metadata::ClassificationT &classification) {
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
    const opk::Color color = hasCameraContact ? opk::Colors::lime : opk::Colors::red;
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

void drawCameraContactMarkers(opk::raster::SurfacePainter &painter,
                              const DebugOverlaySurface &surface,
                              const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::ClassificationsT>(
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

void drawPersonPresence(opk::raster::SurfacePainter &painter,
                        const DebugOverlaySurface &surface,
                        const open_perception_kit::metadata::PersonPresenceT &presence) {
    if (surface.width == 0U || surface.height == 0U || opk::Time::utcMs() % 1000U >= 800U) {
        return;
    }

    const bool isPerson = presence.yes_confidence > presence.no_confidence;
    const std::string_view label = isPerson ? "PERSON" : "NON-PERSON";
    const opk::Color color = isPerson ? PersonPresenceColor : NoPersonPresenceColor;
    const int scale = personTextScaleForHeight(surface.height);
    const auto centerX = static_cast<int>(
        std::min<std::uint32_t>(surface.width / 2U, std::numeric_limits<int>::max()));
    const auto centerY = static_cast<int>(
        std::min<std::uint32_t>(surface.height / 2U, std::numeric_limits<int>::max()));

    painter.drawText(
        centerX, centerY, label, color, opk::Colors::black, scale, opk::raster::TextAnchor::Center);
}

void drawPersonClassifications(opk::raster::SurfacePainter &painter,
                               const DebugOverlaySurface &surface,
                               const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::ClassificationsT>(
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

int drawClassificationList(opk::raster::SurfacePainter &painter,
                           const DebugOverlaySurface &surface,
                           bool alignRight,
                           int bottomOffset,
                           std::string_view heading,
                           const open_perception_kit::metadata::ClassificationT &classification) {
    const int scale = textScaleForHeight(surface.height);
    const int lineHeight = classificationLineHeight(scale);
    const int width = surfaceDimensionToInt(surface.width);
    const auto height = surfaceDimensionToInt(surface.height);
    const auto numResults = static_cast<int>(classification.candidates.size());
    const int lineX = alignRight ? std::max(ClassificationPadding, width - ClassificationPadding)
                                 : ClassificationPadding;
    const int startY =
        height - bottomOffset - (numResults * lineHeight) - (2 * ClassificationPadding);
    const auto anchor =
        alignRight ? opk::raster::TextAnchor::TopRight : opk::raster::TextAnchor::TopLeft;

    if (!heading.empty()) {
        painter.drawText(lineX,
                         std::max(ClassificationPadding, startY - lineHeight),
                         heading,
                         opk::Colors::white,
                         opk::Colors::black,
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
                         opk::Colors::white,
                         opk::Colors::black,
                         scale,
                         anchor);
    }

    return (numResults * lineHeight) + (heading.empty() ? 0 : lineHeight) +
           (2 * ClassificationPadding);
}

void drawImageClassifications(opk::raster::SurfacePainter &painter,
                              const DebugOverlaySurface &surface,
                              const open_perception_kit::FrameResults &frameResults) {
    int leftBottomOffset = 0;
    int rightBottomOffset = 0;
    frameResults.for_each<open_perception_kit::metadata::ClassificationsT>(
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

void drawPerformanceOverlay(opk::raster::SurfacePainter &painter,
                            const DebugOverlaySurface &surface,
                            const open_perception_kit::FrameResults &frameResults) {
    if (surface.height == 0U) {
        return;
    }

    int lineY = PerformanceYOffset;
    const int textScale = textScaleForHeight(surface.height);
    const int lineHeight = textLineHeight(textScale);
    const auto height =
        static_cast<int>(std::min<std::uint32_t>(surface.height, std::numeric_limits<int>::max()));
    frameResults.for_each<open_perception_kit::metadata::PerformanceOverlayT>(
        [&painter, height, textScale, lineHeight, &lineY](const auto &payload) {
            const auto lines = alignPerformanceLines(payload.lines);
            for (const auto &line : lines) {
                if (lineY >= height) {
                    return;
                }

                painter.drawText(PerformanceXOffset,
                                 lineY,
                                 line,
                                 opk::Colors::lime,
                                 opk::Colors::black,
                                 textScale);
                lineY += lineHeight;
            }
        });
}

} // namespace

bool supportsDebugOverlayFormat(opk::RawImagePixelFormat format) noexcept {
    using enum opk::RawImagePixelFormat;

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

DebugOverlayStatus drawDebugOverlay(const DebugOverlayRequest &request) noexcept {
    if (request.frameResults == nullptr) {
        return DebugOverlayStatus::MissingFrameResults;
    }
    if (!supportsDebugOverlayFormat(request.surface.format)) {
        return DebugOverlayStatus::UnsupportedFormat;
    }

    opk::raster::SurfacePainter painter(request.surface.format,
                                        request.surface.width,
                                        request.surface.height,
                                        request.surface.planes,
                                        request.surface.yuvMatrix,
                                        request.surface.yuvRange);
    if (!painter.valid()) {
        return DebugOverlayStatus::InvalidSurface;
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

    return DebugOverlayStatus::Drawn;
}

} // namespace opk::osd
