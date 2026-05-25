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

    virtual const pek::Model &getModel() const override;
    virtual uint8_t *getTensorDataAddress(size_t index) const override;

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;
    virtual pek::Result<void> process(pek::op::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<onnx::Inference> inference;
    std::string modelFamily;
};

} // namespace pek::onnx
