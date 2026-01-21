#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "Inference.h"
#include <memory>

namespace onnx {

class PreprocessInference : public amp::Op {
  public:
    PreprocessInference();
    virtual ~PreprocessInference();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<onnx::Inference> inference;
};

} // namespace onnx
