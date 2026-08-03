/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

namespace pek::stdop::postproc {

/**
 * @brief Tensor parser for ImageNet-style classification models.
 *
 * Parses classification probability vectors and extracts top-k class predictions.
 * Produces image classification results as a generated FrameResults payload.
 */
struct ImageNetClassificationParser : public pek::TensorParser {

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
