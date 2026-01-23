#pragma once

#include <deque>
#include <stdint.h>
#include <string>
#include <uniflow/detection_types.h>
#include <uniflow/labels.h>
#include <vector>
namespace amp {

struct RawDetectionRect2d {
    float x, y, w, h;
    float confidence = 0.0f;
    std::string label;
};

struct RawDetectionPoint2d {
    float x, y;
};

struct RawDetection {
    RawDetection(const uflw::DetectionResult &detectionResult,
                 const std::string &_inferenceElementId,
                 const std::string &_inferenceNetworkId,
                 uflw::LabelType labelType = uflw::LabelType::Coco)
        : inferenceElementId(_inferenceElementId), inferenceNetworkId(_inferenceNetworkId),
          inferenceTime(detectionResult.inferTs) {
        for (const auto &rect : detectionResult.rects) {
            rects.emplace_back(rect.x,
                               rect.y,
                               rect.w,
                               rect.h,
                               0.f,
                               std::string{uflw::Labels::getLabel(labelType, rect.classIndex)});
        }
        for (const auto &point : detectionResult.points) {
            points.emplace_back(point.x, point.y);
        }
    }
    std::string inferenceElementId, inferenceNetworkId;
    uint64_t inferenceTime;
    std::string detectionResultType;

    std::vector<RawDetectionRect2d> rects;
    std::vector<RawDetectionPoint2d> points;

    bool empty() const {
        return rects.empty() && points.empty();
    }
};

// complex object that stores all the inference information
// like raw detections, processed detections
// and all other inference-related data
struct PerceptionContext {
    std::deque<RawDetection> rawDetections;
    std::vector<std::string> perfdata;

    bool empty() const {
        return rawDetections.empty() ||
               std::all_of(rawDetections.begin(), rawDetections.end(), [](const RawDetection &rd) {
                   return rd.empty();
               });
    }
};

} // namespace amp