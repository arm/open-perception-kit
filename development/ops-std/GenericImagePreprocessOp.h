/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Model.h"
#include "pek/Result.h"
#include "pek/Types.h"

#include "preproc/GenericImageTensorBuilder.h"
#include <cstdint>

namespace pek {

class GenericImagePreprocessOp : public pek::Op {
  public:
    GenericImagePreprocessOp();
    virtual ~GenericImagePreprocessOp();

    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    virtual pek::Result<void> process(pek::OpChainContext &opChainContext) override;
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::Op *> &ops) override;

  private:
    std::string inputImageSourceName;
    size_t inputImageTensorIndex;

    pek::GenericImageTensorBuilder genericImageInputTensorBuilder;

    pek::Model upcomingInferenceModel;
    uint8_t *upcomingTensorAddresses[pek::MaxTensorCount] = {nullptr};
};

} // namespace pek
