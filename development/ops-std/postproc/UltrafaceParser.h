/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

//
namespace pek::stdop::postproc {

/**
 * @brief Face detection parser.
 *
 * Used to detect human face rectangles on an image.
 */
struct UltraFaceParser : public pek::TensorParser {

    /**
     * @brief Parses UltraFace detection output tensor.
     *
     * @param input Input tensor containing face detections.
     * @param output Perception layer populated with detected faces.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
