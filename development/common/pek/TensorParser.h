/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/AttributeMap.h"
#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorView.h"

namespace pek {

// Interface for all output tensor parsers
// the parsers create raw metadata from the output tensor memory block
// and place them in
struct TensorParser {

    struct Input {

        Perception::Layer perceptionLayer;

        Input(const pek::AttributeMap &attributes) : attributes(attributes) {}

        pek::TensorView *tensors[pek::MaxTensorCount] = {nullptr};
        const pek::AttributeMap &attributes;
        pek::InferenceInfo inferenceInfo;
    };

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) = 0;
};
} // namespace pek