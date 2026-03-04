/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "amp/TensorParser.h"

namespace amp {

class GenericPostprocessOp : public amp::Op {
  public:
    GenericPostprocessOp();
    virtual ~GenericPostprocessOp();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;
    virtual amp::Result<void> bind(size_t index, const std::vector<amp::Op *> &ops) override;

  private:
    std::unique_ptr<amp::TensorParser> parser;
    amp::AttributeMap attributes;
};

} // namespace amp
