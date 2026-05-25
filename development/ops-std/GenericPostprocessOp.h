/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "pek/TensorParser.h"

namespace pek::stdop {

class GenericPostprocessOp : public pek::op::Op {
  public:
    GenericPostprocessOp();
    virtual ~GenericPostprocessOp();

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> process(pek::op::OpChainContext &opChainContext) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;

  private:
    std::unique_ptr<pek::TensorParser> parser;
    pek::AttributeMap attributes;
};

} // namespace pek::stdop
