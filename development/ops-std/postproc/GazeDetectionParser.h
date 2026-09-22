/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"

namespace opk::stdop::postproc {

/**
 * @brief Tensor parser for gaze direction estimation.
 *
 * Parses gaze estimation model outputs to extract eye gaze direction vectors.
 */
struct GazeDetectionParser : public opk::TensorParser {
    static constexpr std::string_view k_content_type = "eyeYawPitch";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            perception::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
