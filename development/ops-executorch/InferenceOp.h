/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "Inference.h"

namespace pek::extrch {

class InferenceOp : public pek::op::Op {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;
    virtual pek::Result<pek::op::OpSignal>
    process(pek::op::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<pek::extrch::Inference> inference;
    std::string modelFamily;
};

} // namespace pek::extrch
