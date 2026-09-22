/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"

namespace opk::stdop::postproc {

/**
 * @brief Tensor parser for person attribute classification.
 *
 * Parses person classification model outputs to determine if it is a person or not.
 */
struct PersonClassificationParser : public opk::TensorParser {
    static constexpr std::string_view k_content_type = "personClassification";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
