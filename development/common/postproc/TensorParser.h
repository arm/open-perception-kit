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

    // Information about the inference itself
    // sometimes these things are crucial for the parsing itself
    struct ImageInferenceMetadata {
        // physical image dimensions the inference runs on
        size_t width = 0, height = 0;
        // the input tensor dimensions
        size_t modelWidth = 0, modelHeight = 0;
    };

    struct InferenceInfo {
        ImageInferenceMetadata image;
    };

    struct Input {
        amp::TensorView *tensors[4] = {nullptr};
        TensorParser::Settings parserSettings;
        TensorParser::InferenceInfo inferenceInfo;
    };

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::DetectionResult &detectionResult) = 0;
};
} // namespace amp