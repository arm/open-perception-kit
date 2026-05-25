/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"

//
namespace pek::postproc::parser {

struct UltraFaceParser : public pek::TensorParser {

    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) override;
};

} // namespace pek::postproc::parser
