/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/TensorParser.h"

namespace opk::stdop::postproc {

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
    opk::Result<void> parse(const Input &input,
                            open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
