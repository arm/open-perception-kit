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
 * @brief Face detection parser.
 *
 * Used to detect human face rectangles on an image.
 */
struct UltraFaceParser : public pek::TensorParser {

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
