/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "Inference.h"

namespace exct {

class InferenceOp : public amp::Op {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> bind(size_t index, const std::vector<amp::Op *> &ops) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<exct::Inference> inference;
    std::string modelFamily;
};

} // namespace exct
