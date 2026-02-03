#pragma once

#include "amp/AttributeMap.h"
#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorView.h"

namespace amp {

// Interface for all output tensor parsers
// the parsers create raw metadata from the output tensor memory block
// and place them in
struct TensorParser {

    struct Input {

        Perception::Layer perceptionLayer;

        Input(const amp::AttributeMap &attributes) : attributes(attributes) {}

        amp::TensorView *tensors[amp::MaxTensorCount] = {nullptr};
        const amp::AttributeMap &attributes;
        amp::InferenceInfo inferenceInfo;
    };

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::RawDetectionLayer &output) = 0;
};
} // namespace amp