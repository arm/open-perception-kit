/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Perception.h"
#include "amp/Result.h"
#include "amp/TensorParser.h"
#include "amp/TensorView.h"

namespace amp {

struct GazeDetectionParser : public amp::TensorParser {

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::Perception::Layer &output) override;
};

} // namespace amp
