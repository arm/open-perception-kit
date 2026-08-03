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
    pek::Result<void> parse(const Input &input, perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
