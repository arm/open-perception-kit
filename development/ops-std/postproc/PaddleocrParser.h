/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

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

    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
