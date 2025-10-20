#pragma once

#include <vector>

#include "internal_types.h"

namespace uflw {

    // math stuff
    namespace m {

        float sigmoid(float x);
        float intersectionOverUnionXyxy(const DetectionBox& a, const DetectionBox& b);
        std::vector<DetectionBox> nonMaxSupression(const std::vector<DetectionBox>& values, float iouThreshold, int maxKeep);

    };
    
}

