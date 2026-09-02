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
    static constexpr std::string_view k_content_type = "segmentation";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }
    pek::Result<void> parse(const Input &input, perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
