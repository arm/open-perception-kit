/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
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

    /**
     * @brief Parses camera contact detection output.
     *
     * @param input Input tensor containing contact probability.
     * @param output Perception layer populated with contact detection result.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc