/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "postproc/PaddleOcrDbDetectionParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opk::stdop::postproc {
namespace {

struct Settings {
    double binaryThreshold;
    double confidenceThreshold;
    double unclipRatio;
    int64_t maxCandidates;
    double minBoxSize;
    double minExpandedBoxSize;
};

opk::Result<void> readSettings(const opk::AttributeMap &attributes, Settings &settings) {
    for (const auto *key : {"parser",
                            "binaryThreshold",
                            "confidenceThreshold",
                            "unclipRatio",
                            "maxCandidates",
                            "minBoxSize",
                            "minExpandedBoxSize"}) {
        if (!attributes.contains(key))
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          std::string("PaddleOcrDbDetectionParser: required attribute: ") + key));
    }
    for (const auto &[key, value] : attributes.raw()) {
        if (key == "parser") {
            if (value.isString() && value.asString() == "PaddleOcrDbDetectionParser")
                continue;
        } else if (key == "maxCandidates") {
            if (value.isInt() && value.asInt() > 0) {
                settings.maxCandidates = value.asInt();
                continue;
            }
        } else if (key == "binaryThreshold" || key == "confidenceThreshold" ||
                   key == "unclipRatio" || key == "minBoxSize" || key == "minExpandedBoxSize") {
            if (value.isInt() || value.isDouble()) {
                const double number = value.asDouble();
                if (std::isfinite(number) &&
                    (key == "binaryThreshold" || key == "confidenceThreshold"
                         ? number >= 0.0 && number <= 1.0
                         : number > 0.0 && number <= std::numeric_limits<float>::max())) {
                    if (key == "binaryThreshold")
                        settings.binaryThreshold = number;
                    else if (key == "confidenceThreshold")
                        settings.confidenceThreshold = number;
                    else if (key == "unclipRatio")
                        settings.unclipRatio = number;
                    else if (key == "minBoxSize")
                        settings.minBoxSize = number;
                    else
                        settings.minExpandedBoxSize = number;
                    continue;
                }
            }
        }
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrDbDetectionParser: invalid attribute: " + key));
    }
    return {};
}

opk::Result<void> validateGeometry(const opk::ImageInferenceMetadata &image) {
    // Subtract separately so malformed size_t padding cannot wrap around.
    if (image.width == 0 || image.height == 0 || image.modelWidth == 0 || image.modelHeight == 0 ||
        image.letterboxLeft >= image.modelWidth ||
        image.letterboxRight >= image.modelWidth - image.letterboxLeft ||
        image.letterboxTop >= image.modelHeight ||
        image.letterboxBottom >= image.modelHeight - image.letterboxTop) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "PaddleOcrDbDetectionParser: invalid image or padding metadata"));
    }
    return {};
}

struct Point {
    double x = 0.0;
    double y = 0.0;
};

double dot(Point a, Point b) {
    return a.x * b.x + a.y * b.y;
}

double cross(Point origin, Point a, Point b) {
    return (a.x - origin.x) * (b.y - origin.y) - (a.y - origin.y) * (b.x - origin.x);
}

using Contour = std::vector<Point>;

// Suzuki-style border following without hierarchy: scan outer and hole borders in
// row order, using 8-connected foreground and a zero-padded signed-label map.
std::vector<Contour> findContours(const std::vector<float> &probabilities,
                                  size_t width,
                                  size_t height,
                                  const Settings &settings) {
    const size_t stride = width + 2;
    std::vector<int64_t> labels(stride * (height + 2), 0);
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
            labels[(y + 1) * stride + x + 1] =
                probabilities[y * width + x] > settings.binaryThreshold ? 1 : 0;

    const auto row = static_cast<ptrdiff_t>(stride);
    // Clockwise in image coordinates: E, SE, S, SW, W, NW, N, NE.
    const std::array<ptrdiff_t, 8> offsets{1, row + 1, row, row - 1, -1, -row - 1, -row, -row + 1};
    const auto neighbor = [&offsets](size_t center, size_t direction) {
        return static_cast<size_t>(static_cast<ptrdiff_t>(center) + offsets[direction]);
    };
    std::vector<Contour> contours;
    int64_t borderId = 1;
    for (size_t y = 1; y <= height; ++y) {
        for (size_t x = 1; x <= width; ++x) {
            const size_t start = y * stride + x;
            const bool outer = labels[start] == 1 && labels[start - 1] == 0;
            const bool hole = labels[start] >= 1 && labels[start + 1] == 0;
            if (!outer && !hole)
                continue;

            ++borderId;
            Contour contour;
            size_t first = start;
            for (size_t step = 0; step < 8; ++step) {
                const size_t direction = ((outer ? 4 : 0) + step) % 8;
                const size_t candidate = neighbor(start, direction);
                if (labels[candidate] != 0) {
                    first = candidate;
                    break;
                }
            }
            if (first == start) {
                labels[start] = -borderId;
                contour.push_back({static_cast<double>(x - 1), static_cast<double>(y - 1)});
            } else {
                size_t previous = first;
                size_t current = start;
                for (;;) {
                    size_t direction = 0;
                    while (neighbor(current, direction) != previous)
                        ++direction;
                    size_t next = previous;
                    bool examinedRightZero = false;
                    for (size_t step = 1; step <= 8; ++step) {
                        const size_t searchDirection = (direction + 8 - step) % 8;
                        const size_t candidate = neighbor(current, searchDirection);
                        if (labels[candidate] != 0) {
                            next = candidate;
                            break;
                        }
                        if (searchDirection == 0)
                            examinedRightZero = true;
                    }
                    if (examinedRightZero)
                        labels[current] = -borderId;
                    else if (labels[current] == 1)
                        labels[current] = borderId;

                    contour.push_back({static_cast<double>(current % stride - 1),
                                       static_cast<double>(current / stride - 1)});
                    // A diagonal contact can revisit start before the border closes.
                    if (next == start && current == first)
                        break;
                    previous = current;
                    current = next;
                }
            }
            contours.push_back(std::move(contour));
            if (static_cast<uint64_t>(contours.size()) >=
                static_cast<uint64_t>(settings.maxCandidates))
                return contours;
        }
    }
    return contours;
}

Contour convexHull(Contour points) {
    std::sort(points.begin(), points.end(), [](Point a, Point b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(),
                             points.end(),
                             [](Point a, Point b) { return a.x == b.x && a.y == b.y; }),
                 points.end());
    if (points.size() < 3)
        return points;

    Contour hull;
    for (Point point : points) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), point) <= 0.0)
            hull.pop_back();
        hull.push_back(point);
    }
    const size_t lowerSize = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
        while (hull.size() > lowerSize && cross(hull[hull.size() - 2], hull.back(), *it) <= 0.0)
            hull.pop_back();
        hull.push_back(*it);
    }
    hull.pop_back();
    return hull;
}

struct Rectangle {
    Point u;
    Point v;
    double minU = 0.0;
    double maxU = 0.0;
    double minV = 0.0;
    double maxV = 0.0;

    double width() const {
        return maxU - minU;
    }
    double height() const {
        return maxV - minV;
    }

    std::array<Point, 4> corners() const {
        const auto point = [this](double a, double b) {
            return Point{u.x * a + v.x * b, u.y * a + v.y * b};
        };
        return {point(minU, minV), point(maxU, minV), point(maxU, maxV), point(minU, maxV)};
    }
};

std::optional<Rectangle> minimumRectangle(const Contour &hull) {
    if (hull.size() < 3)
        return std::nullopt;
    double minimumArea = std::numeric_limits<double>::infinity();
    std::optional<Rectangle> best;
    for (size_t i = 0; i < hull.size(); ++i) {
        const Point a = hull[i];
        const Point b = hull[(i + 1) % hull.size()];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (length == 0.0)
            continue;
        Rectangle rectangle;
        rectangle.u = {(b.x - a.x) / length, (b.y - a.y) / length};
        rectangle.v = {-rectangle.u.y, rectangle.u.x};
        rectangle.minU = rectangle.maxU = dot(hull[0], rectangle.u);
        rectangle.minV = rectangle.maxV = dot(hull[0], rectangle.v);
        for (Point point : hull) {
            const double u = dot(point, rectangle.u);
            const double v = dot(point, rectangle.v);
            rectangle.minU = std::min(rectangle.minU, u);
            rectangle.maxU = std::max(rectangle.maxU, u);
            rectangle.minV = std::min(rectangle.minV, v);
            rectangle.maxV = std::max(rectangle.maxV, v);
        }
        const double area = rectangle.width() * rectangle.height();
        if (area < minimumArea) {
            minimumArea = area;
            best = rectangle;
        }
    }
    return best;
}

struct Bounds {
    double left;
    double top;
    double right;
    double bottom;
};

Bounds envelope(const std::array<Point, 4> &corners) {
    Bounds bounds{corners[0].x, corners[0].y, corners[0].x, corners[0].y};
    for (Point point : corners) {
        bounds.left = std::min(bounds.left, point.x);
        bounds.right = std::max(bounds.right, point.x);
        bounds.top = std::min(bounds.top, point.y);
        bounds.bottom = std::max(bounds.bottom, point.y);
    }
    return bounds;
}

double boxScore(const Rectangle &rectangle,
                const std::vector<float> &probabilities,
                size_t width,
                size_t height) {
    const auto bounds = envelope(rectangle.corners());
    const auto xmin =
        static_cast<size_t>(std::clamp(std::floor(bounds.left), 0.0, double(width - 1)));
    const auto xmax =
        static_cast<size_t>(std::clamp(std::ceil(bounds.right), 0.0, double(width - 1)));
    const auto ymin =
        static_cast<size_t>(std::clamp(std::floor(bounds.top), 0.0, double(height - 1)));
    const auto ymax =
        static_cast<size_t>(std::clamp(std::ceil(bounds.bottom), 0.0, double(height - 1)));
    double sum = 0.0;
    size_t count = 0;
    // Include boundary pixel centers and every original probability inside the
    // unexpanded quad, including subthreshold pixels and any holes.
    const double tolerance = 1e-7;
    for (size_t y = ymin; y <= ymax; ++y) {
        for (size_t x = xmin; x <= xmax; ++x) {
            const Point point{static_cast<double>(x), static_cast<double>(y)};
            const double u = dot(point, rectangle.u);
            const double v = dot(point, rectangle.v);
            if (u >= rectangle.minU - tolerance && u <= rectangle.maxU + tolerance &&
                v >= rectangle.minV - tolerance && v <= rectangle.maxV + tolerance) {
                sum += probabilities[y * width + x];
                ++count;
            }
        }
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

bool overlapsImage(const Rectangle &rectangle, const Bounds &image) {
    const auto corners = rectangle.corners();
    const std::array<Point, 4> imageCorners{{{image.left, image.top},
                                             {image.right, image.top},
                                             {image.right, image.bottom},
                                             {image.left, image.bottom}}};
    // Separating axes for both rectangles also reject rotated padding-only boxes
    // whose axis-aligned envelope happens to reach the active image's corner.
    for (Point axis : {Point{1.0, 0.0}, Point{0.0, 1.0}, rectangle.u, rectangle.v}) {
        double minimum = dot(corners[0], axis);
        double maximum = minimum;
        double imageMinimum = dot(imageCorners[0], axis);
        double imageMaximum = imageMinimum;
        for (size_t i = 1; i < corners.size(); ++i) {
            minimum = std::min(minimum, dot(corners[i], axis));
            maximum = std::max(maximum, dot(corners[i], axis));
            imageMinimum = std::min(imageMinimum, dot(imageCorners[i], axis));
            imageMaximum = std::max(imageMaximum, dot(imageCorners[i], axis));
        }
        if (maximum <= imageMinimum || imageMaximum <= minimum)
            return false;
    }
    return true;
}

std::optional<Bounds> sourceBounds(const Rectangle &rectangle,
                                   const opk::ImageInferenceMetadata &image,
                                   size_t mapWidth,
                                   size_t mapHeight) {
    const double mapScaleX = static_cast<double>(image.modelWidth) / mapWidth;
    const double mapScaleY = static_cast<double>(image.modelHeight) / mapHeight;
    const Bounds active{image.letterboxLeft / mapScaleX,
                        image.letterboxTop / mapScaleY,
                        (image.modelWidth - image.letterboxRight) / mapScaleX,
                        (image.modelHeight - image.letterboxBottom) / mapScaleY};
    if (!overlapsImage(rectangle, active))
        return std::nullopt;

    const double scaleX = static_cast<double>(image.width) /
                          (image.modelWidth - image.letterboxLeft - image.letterboxRight);
    const double scaleY = static_cast<double>(image.height) /
                          (image.modelHeight - image.letterboxTop - image.letterboxBottom);
    auto corners = rectangle.corners();
    for (Point &point : corners) {
        point.x = (point.x * mapScaleX - image.letterboxLeft) * scaleX;
        point.y = (point.y * mapScaleY - image.letterboxTop) * scaleY;
    }
    auto bounds = envelope(corners);
    bounds.left = std::floor(std::clamp(bounds.left, 0.0, double(image.width)));
    bounds.top = std::floor(std::clamp(bounds.top, 0.0, double(image.height)));
    bounds.right = std::ceil(std::clamp(bounds.right, 0.0, double(image.width)));
    bounds.bottom = std::ceil(std::clamp(bounds.bottom, 0.0, double(image.height)));
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return std::nullopt;
    return bounds;
}

} // namespace

opk::Result<void> PaddleOcrDbDetectionParser::parse(const opk::TensorParser::Input &input,
                                                    open_perception_kit::FrameResults &results) {
    Settings settings;
    if (auto valid = readSettings(input.attributes, settings); !valid)
        return tl::unexpected(valid.error());
    if (auto valid = validateGeometry(input.inferenceInfo.image); !valid)
        return tl::unexpected(valid.error());
    const auto *tensor = input.tensors[0];
    if (!tensor || tensor->getValueType() != opk::Dtype::Float32)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrDbDetectionParser: expected Float32 tensor 0"));
    const auto shape = tensor->getShape();
    if (shape.rank != 4 || shape.dims[0] != 1 || shape.dims[1] != 1 || shape.dims[2] <= 0 ||
        shape.dims[3] <= 0)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrDbDetectionParser: expected resolved [1,1,H,W]"));
    const auto height = static_cast<size_t>(shape.dims[2]);
    const auto width = static_cast<size_t>(shape.dims[3]);
    const auto maximum = std::numeric_limits<size_t>::max();
    if (height > maximum / width || height * width > maximum / sizeof(float) ||
        width > maximum - 2 || height > maximum - 2 ||
        height + 2 > maximum / sizeof(int64_t) / (width + 2) ||
        (height + 2) * (width + 2) > static_cast<size_t>(PTRDIFF_MAX) || !tensor->isValid() ||
        tensor->getCount() != height * width ||
        tensor->getByteCount() < height * width * sizeof(float))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrDbDetectionParser: invalid tensor storage"));

    std::vector<float> probabilities(height * width);
    for (size_t i = 0; i < probabilities.size(); ++i) {
        const float probability = tensor->get(i);
        if (!std::isfinite(probability) || probability < 0.0f || probability > 1.0f)
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "PaddleOcrDbDetectionParser: probability outside [0,1]"));
        probabilities[i] = probability;
    }

    open_perception_kit::metadata::BoxDetectionsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    for (auto &contour : findContours(probabilities, width, height, settings)) {
        auto rectangle = minimumRectangle(convexHull(std::move(contour)));
        if (!rectangle || std::min(rectangle->width(), rectangle->height()) < settings.minBoxSize)
            continue;
        const double confidence = boxScore(*rectangle, probabilities, width, height);
        if (confidence < settings.confidenceThreshold)
            continue;

        // DB's area * ratio / perimeter; local-axis expansion gives the same
        // enclosing rectangle without a polygon-offset dependency.
        const double distance = rectangle->width() * rectangle->height() * settings.unclipRatio /
                                (2.0 * (rectangle->width() + rectangle->height()));
        rectangle->minU -= distance;
        rectangle->maxU += distance;
        rectangle->minV -= distance;
        rectangle->maxV += distance;
        if (std::min(rectangle->width(), rectangle->height()) < settings.minExpandedBoxSize)
            continue;
        const auto bounds = sourceBounds(*rectangle, input.inferenceInfo.image, width, height);
        if (!bounds)
            continue;

        auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
        detection->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
        detection->box =
            open_perception_kit::makeBoundingBox(static_cast<float>(bounds->left),
                                                 static_cast<float>(bounds->top),
                                                 static_cast<float>(bounds->right - bounds->left),
                                                 static_cast<float>(bounds->bottom - bounds->top));
        detection->confidence = static_cast<float>(confidence);
        detection->class_id = -1;
        payload.detections.push_back(std::move(detection));
    }
    if (!payload.detections.empty())
        results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc
