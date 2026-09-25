/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"

namespace opk::stdop::postproc {

struct ScrfdParser : public opk::TensorParser {
    static constexpr std::string_view k_content_type = "humanFace";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    virtual opk::Result<void> parse(const opk::TensorParser::Input &input,
                                    open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
