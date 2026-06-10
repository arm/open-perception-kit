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
 * @brief Tensor parser for person attribute classification.
 *
 * Parses person classification model outputs to determine if it is a person or not.
 */
struct PersonClassificationParser : public pek::TensorParser {

    /**
     * @brief Parses person classification output tensor.
     *
     * @param input Input tensor containing classification scores.
     * @param output Perception layer populated with person attributes.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
