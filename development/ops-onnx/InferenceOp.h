/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "Inference.h"
#include <memory>

namespace onnx {

class InferenceOp : public pek::Op, public pek::OpInterfaceInference {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    virtual const pek::Model &getModel() const override;
    virtual uint8_t *getTensorDataAddress(size_t index) const override;

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::Op *> &ops) override;
    virtual pek::Result<void> process(pek::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<onnx::Inference> inference;
    std::string modelFamily;
};

} // namespace onnx
