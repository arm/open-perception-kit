/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "opk/Result.h"

#include "Inference.h"

#include <memory>

namespace opk::extrch {

class InferenceOp : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    // Exposes model/input buffer information to generic preprocess ops.
    const opk::Model &getModel() const override;
    uint8_t *getTensorDataAddress(size_t index) const override;

    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<opk::extrch::Inference> inference;
};

} // namespace opk::extrch
