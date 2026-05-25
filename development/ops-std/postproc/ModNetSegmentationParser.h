/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/TensorParser.h"

namespace pek::stdop::postproc {

/**
 * @brief Tensor parser for MODNet background matting/segmentation.
 *
 * Parses segmentation mask output from MODNet models, extracting per-pixel
 * foreground/background predictions for precise subject separation.
 */
class ModNetSegmentationParser : public TensorParser {
  public:
    /**
     * @brief Parses MODNet segmentation output tensor.
     *
     * @param input Input tensor containing per-pixel segmentation scores.
     * @param layer Perception layer populated with segmentation mask.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const Input &input, Perception::Layer &layer) override;
};

} // namespace pek::stdop::postproc
