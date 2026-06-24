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
 * @brief Tensor parser for gaze direction estimation.
 *
 * Parses gaze estimation model outputs to extract eye gaze direction vectors.
 */
struct GazeDetectionParser : public pek::TensorParser {

    /**
     * @brief Parses gaze estimation output tensor.
     *
     * @param input Input tensor containing gaze direction data.
     * @param output Perception layer populated with gaze results.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
