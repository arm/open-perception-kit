#pragma once

#include "amp/Model.h"
#include "amp/Result.h"
#include "amp/Types.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include "preproc/GenericImageTensorBuilder.h"
#include <cstdint>

namespace amp {

class GenericImagePreprocessOp : public amp::Op {
  public:
    GenericImagePreprocessOp();
    virtual ~GenericImagePreprocessOp();

    virtual amp::Result<void> configure(const amp::AttributeMap &attributes) override;
    virtual amp::Result<void> process(amp::OpChainContext &opChainContext) override;
    virtual amp::Result<void> bind(size_t index, const std::vector<amp::Op *> &ops) override;

  private:
    std::string inputImageSourceName;
    size_t inputImageTensorIndex;

    amp::GenericImageTensorBuilder genericImageInputTensorBuilder;

    amp::Model upcomingInferenceModel;
    uint8_t *upcomingTensorAddresses[amp::MaxTensorCount] = {nullptr};
};

} // namespace amp
