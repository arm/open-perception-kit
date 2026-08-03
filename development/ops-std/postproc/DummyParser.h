/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

//
namespace pek::stdop::postproc {

/**
 * @brief Dummy tensor parser.
 *
 * Used to investigate network output before implementing a real parser.
 */
struct DummyParser : public pek::TensorParser {

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) override;
};

} // namespace pek::stdop::postproc
