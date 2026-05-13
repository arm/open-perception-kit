/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "Inference.h"

namespace exct {

class InferenceOp : public pek::Op {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::Op *> &ops) override;
    virtual pek::Result<void> process(pek::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<exct::Inference> inference;
    std::string modelFamily;
};

} // namespace exct
