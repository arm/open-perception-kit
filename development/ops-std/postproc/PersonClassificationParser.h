/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

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
    static constexpr std::string_view k_content_type = "personClassification";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
