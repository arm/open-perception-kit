/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"

namespace opk::stdop::postproc {

/**
 * @brief Tensor parser for object embedding/metric learning models.
 *
 * Extracts fixed-size vector embeddings for objects (e.g., person re-identification,
 * vehicle re-identification). Embeddings can be used for similarity matching.
 */
struct ObjectEmbeddingParser : public TensorParser {
    static constexpr std::string_view k_content_type = "objectEmbedding";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    Result<void> parse(const TensorParser::Input &input,
                       perception::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
