/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "opk/ClassificationScores.h"
#include "opk/Color.h"
#include "opk/Tools.h"
#include "opk/Tracking.h"
#include "raster/BitmapFont.h"
#include "raster/SegmentationMask.h"
#include "raster/SurfacePainter.h"
#include <map>
#include <tuple>

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

constexpr const char *BottomRightCompositingMode = "bottomRight";
constexpr int ObjectBoxThickness = 2;
constexpr int CircleMarkerThickness = 2;
constexpr opk::Color CircleMarkerColor = opk::colorFromRgbBytes(38, 0, 255);
constexpr float GazeVectorLength = 120.0f;
constexpr float GazeDeadZoneDegrees = 0.1f;
constexpr int GazeVectorThickness = 2;
constexpr float GazeArrowHeadLength = 12.0f;
constexpr float GazeArrowHeadWidth = 8.0f;
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

bool drawsCircle(const open_perception_kit::metadata::LayerInfoT *const layer,
                 const DebugOverlayOptions &options) noexcept {
    return layer && layer->producer &&
           std::ranges::find(options.circleProducers, layer->producer->instance_id) !=
               options.circleProducers.end();
}

bool drawsRectangle(const open_perception_kit::metadata::LayerInfoT *const layer,
                    const DebugOverlayOptions &options) noexcept {
    return !drawsCircle(layer, options) &&
           (!options.boxProducers ||
            (layer && layer->producer &&
             std::ranges::find(*options.boxProducers, layer->producer->instance_id) !=
                 options.boxProducers->end()));
}

bool isBottomRightLayer(const open_perception_kit::metadata::LayerInfoT *layer) noexcept {
    return layer != nullptr && layer->compositing_mode == BottomRightCompositingMode;
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

std::vector<std::string>
alignPerformanceRows(const std::vector<std::pair<std::string, std::string>> &rows) {
    std::size_t maxLabelGlyphs = 0;
    for (const auto &[label, value] : rows)
        maxLabelGlyphs = std::max(maxLabelGlyphs, textGlyphCount(trimTrailingSpaces(label)));

    std::vector<std::string> aligned;
    aligned.reserve(rows.size());

    std::size_t maxLineGlyphs = 0;
    for (const auto &[label, value] : rows) {
        const auto trimmedLabel = trimTrailingSpaces(label);
        std::string line(trimmedLabel);
        const auto labelGlyphs = textGlyphCount(trimmedLabel);
        line.append(maxLabelGlyphs - std::min(maxLabelGlyphs, labelGlyphs), ' ');
        line.append(": ");
        line.append(value);

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

int roundToInt(const double value) noexcept {
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
    const auto projected =
        open_perception_kit::projectBoundingBox(box, surface.width, surface.height);
    if (!projected) {
        return false;
    }

    const int x0 = clampRasterEdge((*projected)[0], surface.width);
    const int y0 = clampRasterEdge((*projected)[1], surface.height);
    const int x1 = clampRasterEdge((*projected)[0] + (*projected)[2], surface.width);
    const int y1 = clampRasterEdge((*projected)[1] + (*projected)[3], surface.height);
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
            for (const auto &mask : payload.masks) {
                if (!mask || !open_perception_kit::validMask(*mask)) {
                    continue;
                }
                const auto region = open_perception_kit::projectBitmap(
                    *mask->bitmap, surface.width, surface.height);
                if (!region)
                    continue;
                auto maskView = makeMaskView(*mask->bitmap);
                std::vector<uint8_t> projected;
                const auto &[left, top, width, height] = *region;
                const auto &space = *mask->bitmap->space;
                if (space.origin_x != 0 || space.origin_y != 0 ||
                    space.extent_width != space.width || space.extent_height != space.height) {
                    // The existing raster operations consume a full-surface opacity plane.
                    // Outside this mask's declared extent opacity is zero, never stretched.
                    // ponytail: one surface-sized scratch plane for crops; use ROI sampling if
                    // profiling warrants it.
                    projected.resize(static_cast<size_t>(surface.width) * surface.height, 0);
                    for (uint32_t y = 0; y < surface.height; ++y) {
                        const double v = (y + .5 - top) / height;
                        if (v < 0 || v >= 1)
                            continue;
                        const auto sy =
                            std::min(static_cast<size_t>(v * maskView.height), maskView.height - 1);
                        for (uint32_t x = 0; x < surface.width; ++x) {
                            const double u = (x + .5 - left) / width;
                            if (u < 0 || u >= 1)
                                continue;
                            const auto sx = std::min(static_cast<size_t>(u * maskView.width),
                                                     maskView.width - 1);
                            projected[static_cast<size_t>(y) * surface.width + x] =
                                maskView.data[sy * maskView.width + sx];
                        }
                    }
                    maskView = {.data = projected.data(),
                                .size = projected.size(),
                                .width = surface.width,
                                .height = surface.height};
                }
                if (mask->meaning ==
                    open_perception_kit::metadata::OpacityMeaning::BackgroundOpacity) {
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

void drawTrackTrace(opk::raster::SurfacePainter &painter,
                    const DebugOverlaySurface &surface,
                    const open_perception_kit::metadata::TrackTraceT &trace) noexcept {
    if (trace.points.size() < 2U) {
        return;
    }

    if (!open_perception_kit::validImageSpace(trace.space.get()))
        return;
    const auto &space = *trace.space;
    const opk::Color color = colorForTrack(trace.identity->track_id);
    bool hasPrevious = false;
    int previousX = 0;
    int previousY = 0;
    for (const auto &point : trace.points) {
        const int x = roundToInt((space.origin_x + point->x) * surface.width / space.width);
        const int y = roundToInt((space.origin_y + point->y) * surface.height / space.height);
        if (hasPrevious) {
            painter.drawLine(previousX, previousY, x, y, color, TrackTraceThickness);
        }

        previousX = x;
        previousY = y;
        hasPrevious = true;
    }
}

void drawTrackTraces(opk::raster::SurfacePainter &painter,
                     const DebugOverlaySurface &surface,
                     const open_perception_kit::FrameResults &frameResults) {
    using Key = std::tuple<std::string, std::string, uint64_t>;
    const auto key = [](const auto &identity) {
        return Key{identity.tracker_id, identity.session_id, identity.track_id};
    };
    std::map<Key, size_t> counts;
    std::map<Key, std::vector<const open_perception_kit::metadata::ObjectTrackT *>> tracks;
    frameResults.for_each<open_perception_kit::metadata::TrackTracesT>([&](const auto &payload) {
        for (const auto &trace : payload.traces)
            if (trace && opk::tracking::validIdentity(trace->identity.get()))
                ++counts[key(*trace->identity)];
    });
    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>([&](const auto &payload) {
        for (const auto &track : payload.tracks)
            if (track && opk::tracking::validIdentity(track->identity.get()))
                tracks[key(*track->identity)].push_back(track.get());
    });
    frameResults.for_each<open_perception_kit::metadata::TrackTracesT>([&](const auto &payload) {
        for (const auto &trace : payload.traces) {
            if (!trace || !opk::tracking::validTrace(*trace) || counts[key(*trace->identity)] != 1)
                continue;
            const auto linked = tracks.find(key(*trace->identity));
            if (linked != tracks.end()) {
                if (linked->second.size() != 1 || !opk::tracking::validTrack(*linked->second[0]) ||
                    trace->points.empty())
                    continue;
                const auto &a = *linked->second[0]->sample;
                const auto &b = *trace->points.back()->sample;
                if (a.frame_index != b.frame_index || a.state != b.state ||
                    a.time_source != b.time_source || std::abs(a.elapsed_ms - b.elapsed_ms) > 1e-6)
                    continue;
            }
            drawTrackTrace(painter, surface, *trace);
        }
    });
}

std::set<std::uint64_t>
collectTrackedSourceIds(const open_perception_kit::FrameResults &frameResults,
                        const DebugOverlayOptions &options) {
    std::set<std::uint64_t> result;

    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
        [&result, &options](const auto &payload) {
            if (!drawsCircle(payload.layer.get(), options) &&
                !drawsRectangle(payload.layer.get(), options)) {
                return;
            }

            for (const auto &track : payload.tracks) {
                if (track && opk::tracking::validTrack(*track) && track->source_id &&
                    track->sample->state ==
                        open_perception_kit::metadata::TrackObservationState::Observed) {
                    result.insert(*track->source_id);
                }
            }
        });

    return result;
}

bool isTrackedSourceDetection(const open_perception_kit::metadata::BoxDetectionT &detection,
                              const std::set<std::uint64_t> &trackedSourceIds) {
    return detection.object != nullptr && trackedSourceIds.contains(detection.object->id);
}

void drawCircleMarker(opk::raster::SurfacePainter &painter,
                      const DebugOverlaySurface &surface,
                      const open_perception_kit::metadata::BoundingBoxT &box) noexcept {
    PixelBox pixelBox;
    if (!makePixelBox(surface, box, pixelBox)) {
        return;
    }

    const int cx = pixelBox.x + pixelBox.width / 2;
    const int cy = pixelBox.y + pixelBox.height / 2;
    const int radius = std::max(1, pixelBox.width / 2);
    painter.drawCircle(cx, cy, radius, CircleMarkerColor, CircleMarkerThickness);
}

void drawCircleDetections(opk::raster::SurfacePainter &painter,
                          const DebugOverlaySurface &surface,
                          const open_perception_kit::FrameResults &frameResults,
                          const std::set<std::uint64_t> &trackedSourceIds,
                          const DebugOverlayOptions &options) {
    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
        [&painter, &surface, &trackedSourceIds, &options](const auto &payload) {
            if (!drawsCircle(payload.layer.get(), options)) {
                return;
            }

            for (const auto &detection : payload.detections) {
                if (!detection || !detection->box ||
                    isTrackedSourceDetection(*detection, trackedSourceIds)) {
                    continue;
                }

                drawCircleMarker(painter, surface, *detection->box);
            }
        });
}

void drawCircleTracks(opk::raster::SurfacePainter &painter,
                      const DebugOverlaySurface &surface,
                      const open_perception_kit::FrameResults &frameResults,
                      const DebugOverlayOptions &options) {
    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
        [&painter, &surface, &options](const auto &payload) {
            if (!drawsCircle(payload.layer.get(), options)) {
                return;
            }

            for (const auto &track : payload.tracks) {
                if (!track || !opk::tracking::validTrack(*track)) {
                    continue;
                }

                drawCircleMarker(painter, surface, *track->box);
            }
        });
}

void drawCircles(opk::raster::SurfacePainter &painter,
                 const DebugOverlaySurface &surface,
                 const open_perception_kit::FrameResults &frameResults,
                 const DebugOverlayOptions &options) {
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults, options);
    drawCircleDetections(painter, surface, frameResults, trackedSourceIds, options);
    drawCircleTracks(painter, surface, frameResults, options);
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

void drawRectangleDetections(opk::raster::SurfacePainter &painter,
                             const DebugOverlaySurface &surface,
                             const open_perception_kit::FrameResults &frameResults,
                             const std::set<std::uint64_t> &trackedSourceIds,
                             const DebugOverlayOptions &options) {
    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
        [&painter, &surface, &trackedSourceIds, &options](const auto &payload) {
            if (!drawsRectangle(payload.layer.get(), options)) {
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

void drawRectangleTracks(opk::raster::SurfacePainter &painter,
                         const DebugOverlaySurface &surface,
                         const open_perception_kit::FrameResults &frameResults,
                         const DebugOverlayOptions &options) {
    frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
        [&painter, &surface, &options](const auto &payload) {
            if (!drawsRectangle(payload.layer.get(), options)) {
                return;
            }

            for (const auto &track : payload.tracks) {
                if (!track || !opk::tracking::validTrack(*track)) {
                    continue;
                }

                std::string label;
                try {
                    label = opk::tracking::label(*track, payload.display_identity);
                } catch (const std::format_error &) {
                    // Keep the original label if formatting fails at the noexcept display boundary.
                    label = track->text;
                }
                drawLabelledBox(painter, surface, *track->box, label);
            }
        });
}

void drawLabelledBoxes(opk::raster::SurfacePainter &painter,
                       const DebugOverlaySurface &surface,
                       const open_perception_kit::FrameResults &frameResults,
                       const DebugOverlayOptions &options) {
    const auto trackedSourceIds = collectTrackedSourceIds(frameResults, options);
    drawRectangleDetections(painter, surface, frameResults, trackedSourceIds, options);
    drawRectangleTracks(painter, surface, frameResults, options);
}

void drawGazeVector(opk::raster::SurfacePainter &painter,
                    const DebugOverlaySurface &surface,
                    const open_perception_kit::FrameResults &frameResults,
                    const open_perception_kit::metadata::GazeMeasurementT &gaze) {
    if (!gaze.subject_id || !gaze.angles || !std::isfinite(gaze.angles->yaw_degrees) ||
        !std::isfinite(gaze.angles->pitch_degrees) ||
        (gaze.confidence && !opk::classification::validProbability(*gaze.confidence))) {
        return;
    }

    const auto *subject = open_perception_kit::findSubjectBox(frameResults, *gaze.subject_id);
    if (subject == nullptr) {
        return;
    }

    const auto box =
        open_perception_kit::projectBoundingBox(*subject, surface.width, surface.height);
    if (!box) {
        return;
    }

    const float x = (*box)[0] + (*box)[2] / 2.0f;
    const float y = (*box)[1] + (*box)[3] / 2.0f;
    const float yaw = std::remainder(gaze.angles->yaw_degrees, 360.0f);
    const float pitch = std::remainder(gaze.angles->pitch_degrees, 360.0f);
    if (std::abs(yaw) < GazeDeadZoneDegrees && std::abs(pitch) < GazeDeadZoneDegrees) {
        return;
    }

    float xEnd = x;
    float yEnd = y;
    gazeEndpoint(x, y, yaw, pitch, GazeVectorLength, xEnd, yEnd);
    drawSimpleArrow(painter, x, y, xEnd, yEnd, opk::Colors::lightGoldenrodYellow);
}

void drawGazeVectors(opk::raster::SurfacePainter &painter,
                     const DebugOverlaySurface &surface,
                     const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::GazeMeasurementsT>(
        [&painter, &surface, &frameResults](const auto &payload) {
            for (const auto &gaze : payload.measurements) {
                if (gaze) {
                    drawGazeVector(painter, surface, frameResults, *gaze);
                }
            }
        });
}

void drawCameraContactMarker(opk::raster::SurfacePainter &painter,
                             const DebugOverlaySurface &surface,
                             const open_perception_kit::FrameResults &frameResults,
                             const open_perception_kit::metadata::CameraContactT &classification) {
    if (!classification.subject_id || !classification.result) {
        return;
    }

    const auto &candidate = *classification.result;
    if (candidate.confidence && !opk::classification::validProbability(*candidate.confidence)) {
        return;
    }

    const auto *subject =
        open_perception_kit::findSubjectBox(frameResults, *classification.subject_id);
    if (subject == nullptr) {
        return;
    }

    const auto box =
        open_perception_kit::projectBoundingBox(*subject, surface.width, surface.height);
    if (!box) {
        return;
    }

    const bool hasCameraContact = candidate.in_contact;
    const opk::Color color = hasCameraContact ? opk::Colors::lime : opk::Colors::red;
    const double baseRadius = std::min((*box)[2], (*box)[3]) * 0.5;
    const double radius = hasCameraContact
                              ? std::clamp<double>(baseRadius * CameraContactRadiusScale,
                                                   CameraContactMinRadius,
                                                   CameraContactMaxRadius)
                              : std::clamp<double>(baseRadius * NoCameraContactRadiusScale,
                                                   NoCameraContactMinRadius,
                                                   NoCameraContactMaxRadius);
    const int thickness =
        hasCameraContact ? CameraContactCircleThickness : NoCameraContactCircleThickness;
    const int pointSize = hasCameraContact ? CameraContactPointSize : NoCameraContactPointSize;
    const int cx = roundToInt((*box)[0] + (*box)[2] * 0.5);
    const int cy = roundToInt((*box)[1] + (*box)[3] * 0.5);

    painter.drawCircle(cx, cy, std::max(1, roundToInt(radius)), color, thickness);
    painter.drawPoint(cx, cy, color, pointSize);
}

void drawCameraContactMarkers(opk::raster::SurfacePainter &painter,
                              const DebugOverlaySurface &surface,
                              const open_perception_kit::FrameResults &frameResults) {
    frameResults.for_each<open_perception_kit::metadata::CameraContactsT>(
        [&painter, &surface, &frameResults](const auto &payload) {
            for (const auto &classification : payload.contacts) {
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

    if (!presence.result || !opk::classification::validPresenceResult(*presence.result))
        return;
    const bool isPerson = presence.result->person_present;
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
    frameResults.for_each<open_perception_kit::metadata::PersonPresencesT>(
        [&painter, &surface](const auto &payload) {
            for (const auto &presence : payload.presences) {
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
    if (classification.candidates.empty())
        return 0;
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
        if (!candidate || !opk::classification::validCandidate(classification, *candidate)) {
            continue;
        }

        const auto label = candidate->text.empty() && candidate->class_id
                               ? std::to_string(*candidate->class_id)
                               : candidate->text;
        std::string text;
        try {
            text = candidate->confidence
                       ? std::format(
                             "#{}: {} ({:.1f}%)", i + 1, label, *candidate->confidence * 100.0f)
                       : std::format("#{}: {}", i + 1, label);
        } catch (const std::format_error &) {
            // A display-formatting failure must not escape the noexcept overlay boundary.
            continue;
        }
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
    frameResults.for_each<open_perception_kit::metadata::PerformanceMeasurementsT>(
        [&painter, height, textScale, lineHeight, &lineY](const auto &payload) {
            const auto lines =
                alignPerformanceRows(open_perception_kit::formatPerformanceRows(payload));
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
    drawTrackTraces(painter, request.surface, *request.frameResults);
    drawCircles(painter, request.surface, *request.frameResults, request.options);
    drawLabelledBoxes(painter, request.surface, *request.frameResults, request.options);
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
