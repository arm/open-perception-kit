/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorParser.h"

namespace pek::stdop::postproc {

/**
 * @brief Tensor parser for PaddleOCR text detection and recognition.
 *
 * Parses PaddleOCR detection and recognition output tensors to extract text
 * locations and recognized character/word sequences.
 */
struct PaddleOcrDetectionParser : public pek::TensorParser {

    /**
     * @brief Parses PaddleOCR output tensor.
     *
     * @param input Input tensor containing detection/recognition output.
     * @param output Perception layer populated with OCR results.
     * @return Result indicating success or parsing error.
     */
    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
