/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "Inference.h"

#include <memory>

namespace pek::extrch {

class InferenceOp : public pek::op::Op, public pek::op::OpInterfaceInference {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    // Exposes model/input buffer information to generic preprocess ops.
    const pek::Model &getModel() const override;
    uint8_t *getTensorDataAddress(size_t index) const override;

    pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;
    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<pek::extrch::Inference> inference;
};

} // namespace pek::extrch
