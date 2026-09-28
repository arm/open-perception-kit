/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"

//
namespace opk::stdop::postproc {

/**
 * @brief Background removal tensor parser.
 *
 * Used to generate segmentation map of the background on a camera frame.
 */
struct RvmParser : public opk::TensorParser {
    static constexpr std::string_view k_content_type = "segmentation";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
