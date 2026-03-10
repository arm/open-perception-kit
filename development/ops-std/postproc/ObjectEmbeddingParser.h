/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Perception.h"
#include "amp/Result.h"
#include "amp/TensorParser.h"
#include "amp/TensorView.h"

namespace amp {

struct ObjectEmbeddingParser : public TensorParser {

    Result<void> parse(const TensorParser::Input &input, Perception::Layer &output) override;
};

} // namespace amp
