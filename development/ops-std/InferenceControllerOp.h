/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

namespace pek::stdop {

class InferenceControllerOp : public pek::op::Op {
  public:
    InferenceControllerOp();
    virtual ~InferenceControllerOp();

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> process(pek::op::OpChainContext &opChainContext) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;

  private:
    std::string contentType;
};

} // namespace pek::stdop
