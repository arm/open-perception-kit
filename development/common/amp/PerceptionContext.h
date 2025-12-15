#pragma once

#include <vector>
#include <string>
#include <stdiont.h>

namespace amp {

    struct RawDetectionRect2d {
        float x, y, w, h;
        float confidence = 0.0f;
        int classInfo = 0;
    };

    struct RawDetectionPoint2d {
        float x, y;
    };

    struct RawDetection {
        std::string inferenceElementId, inferenceNetworkId;
        uint64_t inferenceTime;
        std::string detectionResultType;

        std::vector<RawDetectionRect2d> rects;
        std::vector<RawDetectionPoint2d> points;
    };

    // complex object that stores all the inference information
    // like raw detections, processed detections
    // and all other inference-related data
    struct PerceptionContext {
        std::vector<RawDetection> rawDetections;

        bool empty() {
            if(rawDetections.empty()) return true;
            
            for(const auto& a : rawDetections) {
                if(a.points.empty() == false) return false;
                if(a.rects.empty() == false) return false;
            }

            return true;
        }
    };

    
}