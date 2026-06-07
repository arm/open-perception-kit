/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/YoloXParser.h"

#include "pek/Labels.h"
#include "pek/Perception.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <limits>
#include <vector>

using namespace pek;
using namespace pek::stdop::postproc;

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

static float sigmoid(float v) {
    return 1.0f / (1.0f + std::exp(-v));
}

static float safeExp(float v) {
    return std::exp(std::max(-20.0f, std::min(v, 20.0f)));
}

static float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

static ScoreMode parseScoreMode(const pek::AttributeMap &attributes) {
    const std::string scoreMode = attributes.getStringOrDefault("scoreMode", "objectnessClass");
    if (scoreMode == "classOnly") {
        return ScoreMode::ClassOnly;
    }
    return ScoreMode::ObjectnessClass;
}

static float iou(const Det &a, const Det &b) {
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

static void nms(std::vector<Det> &dets, float iouThreshold) {
    std::sort(dets.begin(), dets.end(), [](const Det &a, const Det &b) { return a.conf > b.conf; });

    std::vector<char> suppressed(dets.size());
    std::vector<Det> keep;
    keep.reserve(dets.size());

    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i])
            continue;

        keep.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!suppressed[j] && iou(dets[i], dets[j]) > iouThreshold) {
                suppressed[j] = 1;
            }
        }
    }

    dets.swap(keep);
}

static std::vector<GridCell> makeYoloXGrid(size_t modelWidth, size_t modelHeight) {
    const int strides[] = {8, 16, 32};

    std::vector<GridCell> grid;
    for (int stride : strides) {
        const size_t gridW = modelWidth / static_cast<size_t>(stride);
        const size_t gridH = modelHeight / static_cast<size_t>(stride);
        for (size_t y = 0; y < gridH; ++y) {
            for (size_t x = 0; x < gridW; ++x) {
                grid.push_back(GridCell{static_cast<int>(x), static_cast<int>(y), stride});
            }
        }
    }
    return grid;
}

static pek::Result<void>
resolveShape(const pek::Shape &shape, int classCount, size_t &candidateCount, bool &rowMajor) {
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
        PEK_ERROR(pek::ErrorFlag::InvalidData,
                  fmt::format("YoloXParser: expected [1,N,{}] or [1,{},N], got {}",
                              valuesPerCandidate,
                              valuesPerCandidate,
                              shape.toString())));
}

} // namespace

Result<void> YoloXParser::parse(const pek::TensorParser::Input &input,
                                pek::Perception::Layer &detectionResult) {
    if (!input.tensors[0]) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "YoloXParser: input tensor is null"));
    }

    const TensorView &tensor = *input.tensors[0];
    const pek::Shape shape = tensor.getShape();

    const int classCount = static_cast<int>(input.attributes.getIntOrDefault(
        "classCount", input.attributes.getIntOrDefault("classes", 80)));
    if (classCount <= 0) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "YoloXParser: classCount must be positive"));
    }

    size_t candidateCount = 0;
    bool rowMajor = true;
    auto shapeResult = resolveShape(shape, classCount, candidateCount, rowMajor);
    if (!shapeResult) {
        return tl::unexpected(shapeResult.error());
    }

    const size_t frameWidth = input.inferenceInfo.image.width;
    const size_t frameHeight = input.inferenceInfo.image.height;
    const size_t modelWidth = input.inferenceInfo.image.modelWidth;
    const size_t modelHeight = input.inferenceInfo.image.modelHeight;
    if (frameWidth == 0 || frameHeight == 0 || modelWidth == 0 || modelHeight == 0) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "YoloXParser: image dimensions are missing"));
    }

    const float confThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25));
    const float iouThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("iouThreshold", 0.45));
    const int64_t maxDetections = input.attributes.getIntOrDefault("maxDetections", 100);
    const bool applyNms = input.attributes.getBoolOrDefault("applyNms", true);
    const bool decoded = input.attributes.getBoolOrDefault("decoded", false);
    const bool scoresAreLogits = input.attributes.getBoolOrDefault("scoresAreLogits", false);
    const ScoreMode scoreMode = parseScoreMode(input.attributes);
    const bool normalizeOutputCoordinates =
        input.attributes.getBoolOrDefault("normalizeOutputCoordinates", true);

    size_t processedCandidateCount = candidateCount;
    std::vector<GridCell> grid;
    if (!decoded) {
        grid = makeYoloXGrid(modelWidth, modelHeight);
        if (grid.size() > candidateCount) {
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format(
                    "YoloXParser: output has {} candidates but grid needs {} for model {}x{}",
                    candidateCount,
                    grid.size(),
                    modelWidth,
                    modelHeight)));
        }
        processedCandidateCount = grid.size();
    }

    const size_t valuesPerCandidate = static_cast<size_t>(classCount + 5);
    auto getValue = [&](size_t candidate, size_t channel) -> float {
        if (rowMajor) {
            return tensor.get(candidate * valuesPerCandidate + channel);
        }
        return tensor.get(channel * candidateCount + candidate);
    };

    const float sx = static_cast<float>(frameWidth) / static_cast<float>(modelWidth);
    const float sy = static_cast<float>(frameHeight) / static_cast<float>(modelHeight);

    std::vector<Det> dets;
    dets.reserve(processedCandidateCount);

    for (size_t i = 0; i < processedCandidateCount; ++i) {
        float cx = getValue(i, 0);
        float cy = getValue(i, 1);
        float bw = getValue(i, 2);
        float bh = getValue(i, 3);

        if (!decoded) {
            const GridCell &cell = grid[i];
            cx = (cx + static_cast<float>(cell.x)) * static_cast<float>(cell.stride);
            cy = (cy + static_cast<float>(cell.y)) * static_cast<float>(cell.stride);
            bw = safeExp(bw) * static_cast<float>(cell.stride);
            bh = safeExp(bh) * static_cast<float>(cell.stride);
        }

        float objectness = getValue(i, 4);
        if (scoresAreLogits) {
            objectness = sigmoid(objectness);
        }

        int bestClass = -1;
        float bestClassScore = -std::numeric_limits<float>::infinity();
        for (int c = 0; c < classCount; ++c) {
            float score = getValue(i, static_cast<size_t>(5 + c));
            if (scoresAreLogits) {
                score = sigmoid(score);
            }
            if (score > bestClassScore) {
                bestClassScore = score;
                bestClass = c;
            }
        }

        const float confidence =
            (scoreMode == ScoreMode::ClassOnly) ? bestClassScore : objectness * bestClassScore;

        float x1 = cx - bw * 0.5f;
        float y1 = cy - bh * 0.5f;
        float x2 = cx + bw * 0.5f;
        float y2 = cy + bh * 0.5f;

        x1 *= sx;
        x2 *= sx;
        y1 *= sy;
        y2 *= sy;

        x1 = clampf(x1, 0.0f, static_cast<float>(frameWidth - 1));
        x2 = clampf(x2, 0.0f, static_cast<float>(frameWidth - 1));
        y1 = clampf(y1, 0.0f, static_cast<float>(frameHeight - 1));
        y2 = clampf(y2, 0.0f, static_cast<float>(frameHeight - 1));

        const bool finiteBox =
            std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2) && std::isfinite(y2);

        if (bestClass < 0 || !std::isfinite(confidence) || confidence < confThreshold) {
            continue;
        }

        if (!finiteBox) {
            continue;
        }
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        dets.push_back(Det{x1, y1, x2, y2, confidence, bestClass});
    }

    if (applyNms) {
        nms(dets, iouThreshold);
    } else {
        std::sort(
            dets.begin(), dets.end(), [](const Det &a, const Det &b) { return a.conf > b.conf; });
    }

    if (maxDetections >= 0 && dets.size() > static_cast<size_t>(maxDetections)) {
        dets.resize(static_cast<size_t>(maxDetections));
    }

    detectionResult.contentType = "genericObject";
    for (const auto &det : dets) {
        Perception::Rect rect;
        rect.x = det.x1;
        rect.y = det.y1;
        rect.width = det.x2 - det.x1;
        rect.height = det.y2 - det.y1;
        rect.confidence = det.conf;
        rect.classId = det.cls;
        rect.text = pek::resources::Labels::getLabel(pek::resources::LabelType::Coco, det.cls);

        if (normalizeOutputCoordinates) {
            rect.x /= static_cast<float>(frameWidth);
            rect.width /= static_cast<float>(frameWidth);
            rect.y /= static_cast<float>(frameHeight);
            rect.height /= static_cast<float>(frameHeight);
        }

        detectionResult.detections.push_back(rect);
    }

    return {};
}
