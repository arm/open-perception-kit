#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "postproc/TensorParser.h"

namespace amp {

class PostprocessOp : public amp::Op {
  public:
    PostprocessOp();
    virtual ~PostprocessOp();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;
    virtual amp::Result<void> bind(size_t index, const std::vector<amp::Op *> &ops) override;

  private:
    std::unique_ptr<amp::TensorParser> parser;
};

} // namespace amp
