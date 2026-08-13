/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"

namespace pek::stdop::postproc {

struct YoloXParser : public pek::TensorParser {
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
