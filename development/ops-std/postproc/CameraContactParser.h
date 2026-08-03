/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

namespace pek::stdop::postproc {

/**
 * @brief Tensor parser for camera contact detection.
 *
 * Specialized parser for detecting if the user looks into the camera or not.
 */
struct CameraContactParser : public pek::TensorParser {

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
