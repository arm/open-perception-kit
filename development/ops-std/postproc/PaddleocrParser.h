/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorParser.h"

namespace pek::stdop::postproc {

/**
 * @brief Tensor parser for PaddleOCR text detection.
 *
 * Parses the PaddleOCR detection output tensor into a segmentation mask.
 */
struct PaddleOcrDetectionParser : public pek::TensorParser {

    /**
     * @brief Parses PaddleOCR output tensor.
     *
     * @param input Input tensor containing detection output.
     * @param output Perception layer populated with OCR results.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
