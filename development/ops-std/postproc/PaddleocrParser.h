/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorParser.h"

namespace opk::stdop::postproc {

/**
 * @brief Tensor parser for PaddleOCR text detection and recognition.
 *
 * Parses PaddleOCR detection and recognition output tensors to extract text
 * locations and recognized character/word sequences.
 */
struct PaddleOcrDetectionParser : public opk::TensorParser {
    static constexpr std::string_view k_content_type = "segmentation";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
