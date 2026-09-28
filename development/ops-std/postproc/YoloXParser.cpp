/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "postproc/YoloXParser.h"

#include "opk/FrameResults.h"
#include "opk/Labels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace opk;
using namespace opk::stdop::postproc;

namespace {

struct Det {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float conf = 0.0f;
    int cls = 0;
};

struct GridCell {
    int x = 0;
    int y = 0;
    int stride = 0;
};

enum class ScoreMode {
    ObjectnessClass,
    ClassOnly,
};

struct ImageGeometry {
    size_t frameWidth = 0;
    size_t frameHeight = 0;
    size_t modelWidth = 0;
    size_t modelHeight = 0;
    size_t letterboxLeft = 0;
    size_t letterboxRight = 0;
    size_t letterboxTop = 0;
    size_t letterboxBottom = 0;
};

struct ParserSettings {
    int classCount = 0;
    float confThreshold = 0.0f;
    float iouThreshold = 0.0f;
    int64_t maxDetections = 0;
    bool applyNms = true;
    bool decoded = false;
    bool scoresAreLogits = false;
    ScoreMode scoreMode = ScoreMode::ObjectnessClass;
    bool normalizeOutputCoordinates = true;
};

struct CandidateReader {
    const TensorView &tensor;
    size_t candidateCount = 0;
    size_t valuesPerCandidate = 0;
    bool rowMajor = true;

    float get(size_t candidate, size_t channel) const {
        if (rowMajor) {
            return tensor.get(candidate * valuesPerCandidate + channel);
        }
        return tensor.get(channel * candidateCount + candidate);
    }
};

struct CandidateBox {
    float cx = 0.0f;
    float cy = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ClassScore {
    int classId = -1;
    float score = -std::numeric_limits<float>::infinity();
};

float sigmoid(float v) {
    return 1.0f / (1.0f + std::exp(-v));
}

float safeExp(float v) {
    return std::exp(std::max(-20.0f, std::min(v, 20.0f)));
}

float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

ScoreMode parseScoreMode(const opk::AttributeMap &attributes) {
    if (const auto scoreMode = attributes.getStringOrDefault("scoreMode", "objectnessClass");
        scoreMode == "classOnly") {
        return ScoreMode::ClassOnly;
    }
    return ScoreMode::ObjectnessClass;
}

ParserSettings makeParserSettings(const opk::AttributeMap &attributes, int classCount) {
    return ParserSettings{
        classCount,
        static_cast<float>(attributes.getDoubleOrDefault("confidenceThreshold", 0.25)),
        static_cast<float>(attributes.getDoubleOrDefault("iouThreshold", 0.45)),
        attributes.getIntOrDefault("maxDetections", 100),
        attributes.getBoolOrDefault("applyNms", true),
        attributes.getBoolOrDefault("decoded", false),
        attributes.getBoolOrDefault("scoresAreLogits", false),
        parseScoreMode(attributes),
        attributes.getBoolOrDefault("normalizeOutputCoordinates", true),
    };
}

ImageGeometry makeImageGeometry(const opk::TensorParser::Input &input) {
    return ImageGeometry{
        input.inferenceInfo.image.width,
        input.inferenceInfo.image.height,
        input.inferenceInfo.image.modelWidth,
        input.inferenceInfo.image.modelHeight,
        input.inferenceInfo.image.letterboxLeft,
        input.inferenceInfo.image.letterboxRight,
        input.inferenceInfo.image.letterboxTop,
        input.inferenceInfo.image.letterboxBottom,
    };
}

float scoreValue(float value, bool scoresAreLogits) {
    return scoresAreLogits ? sigmoid(value) : value;
}

float iou(const Det &a, const Det &b) {
    const float xx1 = std::max(a.x1, b.x1);
    const float yy1 = std::max(a.y1, b.y1);
    const float xx2 = std::min(a.x2, b.x2);
    const float yy2 = std::min(a.y2, b.y2);
    const float w = std::max(0.0f, xx2 - xx1);
    const float h = std::max(0.0f, yy2 - yy1);
    const float inter = w * h;
    const float areaA = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float areaB = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    const float uni = areaA + areaB - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

void sortByConfidence(std::vector<Det> &dets) {
    std::ranges::sort(dets, [](const Det &a, const Det &b) { return a.conf > b.conf; });
}

void nms(std::vector<Det> &dets, float iouThreshold) {
    sortByConfidence(dets);

    std::vector<char> suppressed(dets.size());
    std::vector<Det> keep;
    keep.reserve(dets.size());

    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i])
            continue;

        keep.emplace_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!suppressed[j] && iou(dets[i], dets[j]) > iouThreshold) {
                suppressed[j] = 1;
            }
        }
    }

    dets.swap(keep);
}

std::vector<GridCell> makeYoloXGrid(size_t modelWidth, size_t modelHeight) {
    constexpr auto strides = std::array{8, 16, 32};

    std::vector<GridCell> grid;
    for (int stride : strides) {
        const size_t gridW = modelWidth / static_cast<size_t>(stride);
        const size_t gridH = modelHeight / static_cast<size_t>(stride);
        for (size_t y = 0; y < gridH; ++y) {
            for (size_t x = 0; x < gridW; ++x) {
                grid.emplace_back(GridCell{static_cast<int>(x), static_cast<int>(y), stride});
            }
        }
    }
    return grid;
}

opk::Result<void>
resolveShape(const opk::Shape &shape, int classCount, size_t &candidateCount, bool &rowMajor) {
    const int valuesPerCandidate = classCount + 5;

    if (shape.rank == 3 && shape.dims[0] == 1) {
        if (shape.dims[2] == valuesPerCandidate) {
            candidateCount = static_cast<size_t>(shape.dims[1]);
            rowMajor = true;
            return {};
        }
        if (shape.dims[1] == valuesPerCandidate) {
            candidateCount = static_cast<size_t>(shape.dims[2]);
            rowMajor = false;
            return {};
        }
    }

    if (shape.rank == 2) {
        if (shape.dims[1] == valuesPerCandidate) {
            candidateCount = static_cast<size_t>(shape.dims[0]);
            rowMajor = true;
            return {};
        }
        if (shape.dims[0] == valuesPerCandidate) {
            candidateCount = static_cast<size_t>(shape.dims[1]);
            rowMajor = false;
            return {};
        }
    }

    return tl::unexpected(
        OPK_ERROR(opk::ErrorFlag::InvalidData,
                  fmt::format("YoloXParser: expected [1,N,{}] or [1,{},N], got {}",
                              valuesPerCandidate,
                              valuesPerCandidate,
                              shape.toString())));
}

CandidateBox readCandidateBox(const CandidateReader &reader, size_t candidateIndex) {
    return CandidateBox{
        reader.get(candidateIndex, 0),
        reader.get(candidateIndex, 1),
        reader.get(candidateIndex, 2),
        reader.get(candidateIndex, 3),
    };
}

void decodeGridBox(CandidateBox &box, const GridCell &cell) {
    box.cx = (box.cx + static_cast<float>(cell.x)) * static_cast<float>(cell.stride);
    box.cy = (box.cy + static_cast<float>(cell.y)) * static_cast<float>(cell.stride);
    box.width = safeExp(box.width) * static_cast<float>(cell.stride);
    box.height = safeExp(box.height) * static_cast<float>(cell.stride);
}

ClassScore findBestClass(const CandidateReader &reader,
                         size_t candidateIndex,
                         const ParserSettings &settings,
                         size_t classValueOffset) {
    ClassScore best;
    for (int c = 0; c < settings.classCount; ++c) {
        const auto score =
            scoreValue(reader.get(candidateIndex, classValueOffset + static_cast<size_t>(c)),
                       settings.scoresAreLogits);
        if (score > best.score) {
            best.score = score;
            best.classId = c;
        }
    }
    return best;
}

size_t activeModelWidth(const ImageGeometry &geometry) {
    const size_t horizontalPadding = geometry.letterboxLeft + geometry.letterboxRight;
    if (horizontalPadding >= geometry.modelWidth) {
        return geometry.modelWidth;
    }
    return geometry.modelWidth - horizontalPadding;
}

size_t activeModelHeight(const ImageGeometry &geometry) {
    const size_t verticalPadding = geometry.letterboxTop + geometry.letterboxBottom;
    if (verticalPadding >= geometry.modelHeight) {
        return geometry.modelHeight;
    }
    return geometry.modelHeight - verticalPadding;
}

float modelToFrameX(float x, const ImageGeometry &geometry) {
    return (x - static_cast<float>(geometry.letterboxLeft)) *
           static_cast<float>(geometry.frameWidth) / static_cast<float>(activeModelWidth(geometry));
}

float modelToFrameY(float y, const ImageGeometry &geometry) {
    return (y - static_cast<float>(geometry.letterboxTop)) *
           static_cast<float>(geometry.frameHeight) /
           static_cast<float>(activeModelHeight(geometry));
}

Det makeDetection(const CandidateBox &box,
                  float confidence,
                  int classId,
                  const ImageGeometry &geometry) {
    auto x1 = modelToFrameX(box.cx - box.width * 0.5f, geometry);
    auto y1 = modelToFrameY(box.cy - box.height * 0.5f, geometry);
    auto x2 = modelToFrameX(box.cx + box.width * 0.5f, geometry);
    auto y2 = modelToFrameY(box.cy + box.height * 0.5f, geometry);

    x1 = clampf(x1, 0.0f, static_cast<float>(geometry.frameWidth - 1));
    x2 = clampf(x2, 0.0f, static_cast<float>(geometry.frameWidth - 1));
    y1 = clampf(y1, 0.0f, static_cast<float>(geometry.frameHeight - 1));
    y2 = clampf(y2, 0.0f, static_cast<float>(geometry.frameHeight - 1));

    return Det{x1, y1, x2, y2, confidence, classId};
}

bool hasValidGeometry(const Det &det) {
    return std::isfinite(det.x1) && std::isfinite(det.y1) && std::isfinite(det.x2) &&
           std::isfinite(det.y2) && det.x2 > det.x1 && det.y2 > det.y1;
}

std::optional<Det> parseCandidate(const CandidateReader &reader,
                                  size_t candidateIndex,
                                  const std::vector<GridCell> &grid,
                                  const ParserSettings &settings,
                                  const ImageGeometry &geometry) {
    constexpr size_t classValueOffset = 5;

    auto box = readCandidateBox(reader, candidateIndex);
    if (!settings.decoded) {
        decodeGridBox(box, grid[candidateIndex]);
    }

    const auto objectness = scoreValue(reader.get(candidateIndex, 4), settings.scoresAreLogits);
    const auto bestClass = findBestClass(reader, candidateIndex, settings, classValueOffset);
    const auto confidence = (settings.scoreMode == ScoreMode::ClassOnly)
                                ? bestClass.score
                                : objectness * bestClass.score;

    if (bestClass.classId < 0 || !std::isfinite(confidence) ||
        confidence < settings.confThreshold) {
        return std::nullopt;
    }

    auto det = makeDetection(box, confidence, bestClass.classId, geometry);
    if (!hasValidGeometry(det)) {
        return std::nullopt;
    }
    return det;
}

std::vector<Det> collectDetections(const CandidateReader &reader,
                                   size_t processedCandidateCount,
                                   const std::vector<GridCell> &grid,
                                   const ParserSettings &settings,
                                   const ImageGeometry &geometry) {
    std::vector<Det> dets;
    dets.reserve(processedCandidateCount);

    for (size_t i = 0; i < processedCandidateCount; ++i) {
        if (auto det = parseCandidate(reader, i, grid, settings, geometry)) {
            dets.emplace_back(*det);
        }
    }

    return dets;
}

void finalizeDetections(std::vector<Det> &dets, const ParserSettings &settings) {
    if (settings.applyNms) {
        nms(dets, settings.iouThreshold);
    } else {
        sortByConfidence(dets);
    }

    if (settings.maxDetections >= 0 && dets.size() > static_cast<size_t>(settings.maxDetections)) {
        dets.resize(static_cast<size_t>(settings.maxDetections));
    }
}

void appendDetections(const std::vector<Det> &dets,
                      const ParserSettings &settings,
                      const ImageGeometry &geometry,
                      uint64_t parentId,
                      open_perception_kit::metadata::BoxDetectionsT &payload) {
    for (const auto &det : dets) {
        float x = det.x1;
        float y = det.y1;
        float width = det.x2 - det.x1;
        float height = det.y2 - det.y1;

        if (settings.normalizeOutputCoordinates) {
            x /= static_cast<float>(geometry.frameWidth);
            width /= static_cast<float>(geometry.frameWidth);
            y /= static_cast<float>(geometry.frameHeight);
            height /= static_cast<float>(geometry.frameHeight);
        }

        auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
        detection->object = open_perception_kit::makeObjectMeta(0U, parentId);
        detection->box = open_perception_kit::makeBoundingBox(x, y, width, height);
        detection->confidence = det.conf;
        detection->class_id = det.cls;
        detection->text =
            opk::resources::Labels::getLabel(opk::resources::LabelType::Coco, det.cls);
        payload.detections.push_back(std::move(detection));
    }
}

} // namespace

Result<void> YoloXParser::parse(const opk::TensorParser::Input &input,
                                open_perception_kit::FrameResults &results) {
    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "YoloXParser: input tensor is null"));
    }

    const auto &tensor = *input.tensors[0];
    const auto shape = tensor.getShape();

    const auto classCount = static_cast<int>(input.attributes.getIntOrDefault("classCount", 80));
    if (classCount <= 0) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "YoloXParser: classCount must be positive"));
    }

    size_t candidateCount = 0;
    bool rowMajor = true;
    if (auto shapeResult = resolveShape(shape, classCount, candidateCount, rowMajor);
        !shapeResult) {
        return tl::unexpected(shapeResult.error());
    }

    const auto geometry = makeImageGeometry(input);
    if (geometry.frameWidth == 0 || geometry.frameHeight == 0 || geometry.modelWidth == 0 ||
        geometry.modelHeight == 0) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "YoloXParser: image dimensions are missing"));
    }

    const auto settings = makeParserSettings(input.attributes, classCount);

    size_t processedCandidateCount = candidateCount;
    std::vector<GridCell> grid;
    if (!settings.decoded) {
        grid = makeYoloXGrid(geometry.modelWidth, geometry.modelHeight);
        if (grid.size() > candidateCount) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                fmt::format(
                    "YoloXParser: output has {} candidates but grid needs {} for model {}x{}",
                    candidateCount,
                    grid.size(),
                    geometry.modelWidth,
                    geometry.modelHeight)));
        }
        processedCandidateCount = grid.size();
    }

    constexpr size_t classValueOffset = 5;
    const auto reader = CandidateReader{
        tensor, candidateCount, static_cast<size_t>(classCount) + classValueOffset, rowMajor};
    auto dets = collectDetections(reader, processedCandidateCount, grid, settings, geometry);
    finalizeDetections(dets, settings);
    open_perception_kit::metadata::BoxDetectionsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .labelFamily = "coco",
                                            .producer = &input.producerInfo});
    appendDetections(dets, settings, geometry, input.inferenceInfo.parentId, payload);
    if (!payload.detections.empty()) {
        results.add(std::move(payload));
    }

    return {};
}
