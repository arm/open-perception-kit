#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorView.h"

namespace amp {

// Interface for all output tensor parsers
// the parsers create raw metadata from the output tensor memory block
// and place them in
struct TensorParser {

    struct Settings {
        bool normalizedCoordinates = false;
        float confidenceThreshold = 0.0f;
        float iouThreshold = 0.0f;
        bool fixedAnchorSizes = false;
        bool applyNms = true;
        size_t maxDetectionCount = 0;
    };

    struct Input {
        amp::TensorView *tensors[amp::MaxTensorCount] = {nullptr};
        TensorParser::Settings parserSettings;
        amp::InferenceInfo inferenceInfo;
    };

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::RawDetectionLayer &output) = 0;
};
} // namespace amp