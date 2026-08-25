/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

//
namespace pek::stdop::postproc {

/**
 * @brief Background removal tensor parser.
 *
 * Used to generate segmentation map of the background on a camera frame.
 */
struct RvmParser : public pek::TensorParser {

    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
