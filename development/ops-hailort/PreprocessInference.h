#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpContext.h"

#include "Inference.h"

namespace hailort {

class PreprocessInference : public amp::Op {
  public:
    PreprocessInference();
    virtual ~PreprocessInference();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpContext &opContext) override;

  private:
    std::unique_ptr<hailort::Inference> inference;
};

} // namespace hailort
