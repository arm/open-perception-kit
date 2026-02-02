#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "Inference.h"

namespace hailort {

class InferrenceOp : public amp::Op {
  public:
    InferrenceOp();
    virtual ~InferrenceOp();

    virtual amp::Result<void> peek(amp::OpChainContext &opChainContext) override;
    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<hailort::Inference> inference;
};

} // namespace hailort
