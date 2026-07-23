/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "Inference.h"
#include <memory>

namespace pek::onnx {

class InferenceOp : public pek::op::Op, public pek::op::OpInterfaceInference {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    const pek::Model &getModel() const override;
    uint8_t *getTensorDataAddress(size_t index) const override;

    pek::Result<void> configure(const pek::AttributeMap &attributes,
                                pek::op::OpSetupContext &setupContext) override;
    pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;
    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<pek::onnx::Inference> inference;
    std::string modelFamily;
};

} // namespace pek::onnx
