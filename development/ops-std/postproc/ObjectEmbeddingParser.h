/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

namespace pek::stdop::postproc {

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

} // namespace pek::stdop::postproc
