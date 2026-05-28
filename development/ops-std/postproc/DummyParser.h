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
 * @brief Dummy tensor parser.
 *
 * Used to investigate network output before implementing a real parser.
 */
struct DummyParser : public pek::TensorParser {

    /**
     * @brief No-op parser implementation.
     *
     * @param input Tensor input (ignored).
     * @param output Empty Perception layer.
     * @return Always success.
     */
    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) override;
};

} // namespace pek::stdop::postproc
