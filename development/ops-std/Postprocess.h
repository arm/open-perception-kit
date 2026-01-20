#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpContext.h"

#include "postproc/TensorParser.h"

#include "onnx/Inference.h"

namespace amp {

class Postprocess : public amp::Op {
  public:
    Postprocess();
    virtual ~Postprocess();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpContext &opContext) override;

  private:
    std::unique_ptr<amp::TensorParser> parser;
};

} // namespace amp
