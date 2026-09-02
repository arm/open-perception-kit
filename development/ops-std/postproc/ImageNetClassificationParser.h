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
    static constexpr std::string_view k_content_type = "classification";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
