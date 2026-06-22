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
 * @brief Yolo parser.
 *
 * Classic yolo object detection parser.
 */
struct YoloParser : public pek::TensorParser {

    /**
     * @brief Parses YOLO detection output tensor.
     *
     * @param input Input tensor and model information.
     * @param output Perception layer populated with detected objects.
     * @return Result indicating success or parsing error.
     */
    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
