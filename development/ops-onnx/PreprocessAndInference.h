#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "Inference.h"
#include <memory>

namespace onnx {

class PreprocessAndInference : public amp::Op {
  public:
    PreprocessAndInference();
    virtual ~PreprocessAndInference();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> bind(size_t index, const std::vector<amp::Op *> &ops) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<onnx::Inference> inference;
};

} // namespace onnx
