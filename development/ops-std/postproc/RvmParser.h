/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

//
namespace pek::stdop::postproc {

/**
 * @brief Bacgkround removal tensor parser.
 *
 * Used to generate segmentation map of the background on a camera frame.
 */
struct RvmParser : public pek::TensorParser {

    /**
     * @brief Parses RVM segmentation output tensor.
     *
     * @param input Input tensor containing alpha/trimap data.
     * @param output Perception layer populated with matting result.
     * @return Result indicating success or parsing error.
     */
    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
