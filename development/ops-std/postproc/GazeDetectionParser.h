/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

namespace pek::stdop::postproc {

/**
 * @brief Tensor parser for gaze direction estimation.
 *
 * Parses gaze estimation model outputs to extract eye gaze direction vectors.
 */
struct GazeDetectionParser : public pek::TensorParser {
    static constexpr std::string_view k_content_type = "eyeYawPitch";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    pek::Result<void> parse(const pek::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
