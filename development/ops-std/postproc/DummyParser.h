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
 * @brief Dummy tensor parser.
 *
 * Used to investigate network output before implementing a real parser.
 */
struct DummyParser : public opk::TensorParser {
    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {};
    }

    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
