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
 * @brief Tensor parser for ImageNet-style classification models.
 *
 * Parses classification probability vectors and extracts top-k class predictions.
 * Produces image classification results as a Perception::Layer.
 */
struct ImageNetClassificationParser : public pek::TensorParser {

    /**
     * @brief Parses ImageNet classification output tensor.
     *
     * @param input Input tensor containing class probabilities.
     * @param output Perception layer populated with classification results.
     * @return Result indicating success or parsing error.
     */
    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
