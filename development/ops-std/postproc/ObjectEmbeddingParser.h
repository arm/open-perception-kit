/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
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

    /**
     * @brief Parses object embedding output tensor.
     *
     * @param input Input tensor containing embedding vectors.
     * @param output Perception layer populated with embedding data.
     * @return Result indicating success or parsing error.
     */
    Result<void> parse(const TensorParser::Input &input, Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
