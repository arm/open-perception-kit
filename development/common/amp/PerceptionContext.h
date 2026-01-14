#pragma once

#include "amp/Bitmap.h"

#include <cassert>
#include <string>
#include <vector>

#include <stdint.h>

namespace amp {

struct DetectionRect {
    float x, y, w, h;
    float confidence;
    int classIndex;
};

struct DetectionPoint {
    float x, y;
};

struct DetectionResult {
    uint64_t inferId, originTs, inferTs;
    std::vector<DetectionRect> rects;
    std::vector<DetectionPoint> points;
    std::vector<Map8> maps;
};

// ---

struct RawDetectionRect2d {
    float x, y, w, h;
    float confidence = 0.0f;
    int classInfo = 0;
};

struct RawDetectionPoint2d {
    float x, y;
};

struct SegmentationMap {
    std::vector<uint8_t> map;
    size_t width = 0, height = 0;

    inline uint8_t &at(size_t x, size_t y) {
        assert(x < width);
        assert(x < width);
        return map.data()[width * y + x];
    }
};

struct RawDetection {
    std::string inferenceElementId, inferenceNetworkId;
    uint64_t inferenceTime;
    std::string detectionResultType;

    std::vector<RawDetectionRect2d> rects;
    std::vector<RawDetectionPoint2d> points;
    std::vector<SegmentationMap> segmentationMaps;
};

// complex object that stores all the inference information
// like raw detections, processed detections
// and all other inference-related data
struct PerceptionContext {
    std::vector<RawDetection> rawDetections;

    bool empty() {
        if (rawDetections.empty())
            return true;

        for (const auto &a : rawDetections) {
            if (a.points.empty() == false)
                return false;
            if (a.rects.empty() == false)
                return false;
        }

        return true;
    }
};

} // namespace amp