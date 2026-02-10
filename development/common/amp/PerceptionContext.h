#pragma once

#include "amp/Bitmap.h"
#include "amp/Labels.h"
#include <deque>
#include <stdint.h>
#include <string>
#include <vector>

#include "amp/Perception.h"
#ifdef skip
namespace amp {
struct DetectionRect {
    float x, y, w, h;
    float confidence;
    std::string label;
};
struct DetectionPoint {
    float x, y;
};
struct RawDetectionLayer {
    std::string modelFamily;
    uint64_t inferId, originTs, inferTs;
    std::vector<DetectionRect> rects;
    std::vector<DetectionPoint> points;
    std::vector<amp::Bitmap> maps;
};

/*struct SegmentationMap {
    std::vector<uint8_t> map;
    size_t width = 0, height = 0;

    inline uint8_t &at(size_t x, size_t y) {
        assert(x < width);
        assert(x < width);
        return map.data()[width * y + x];
    }
};*/

// complex object that stores all the inference information
// like raw detections, processed detections
// and all other inference-related data
struct PerceptionContext {
    std::deque<RawDetectionLayer> rawDetections;
    std::vector<std::string> perfdata;
    Perception perception;
};
} // namespace amp
#endif
