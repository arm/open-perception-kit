/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/TensorParser.h"

namespace pek::stdop::postproc::parser {

class ModNetSegmentationParser : public TensorParser {
  public:
    pek::Result<void> parse(const Input &input, Perception::Layer &layer) override;
};

} // namespace pek::stdop::postproc::parser
