/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

namespace pek::stdop::postproc {

struct ScrfdParser : public pek::TensorParser {
    static constexpr std::string_view k_content_type = "humanFace";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
