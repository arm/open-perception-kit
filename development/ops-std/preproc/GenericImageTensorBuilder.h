/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Result.h"
#include "amp/TensorBuilder.h"

namespace amp {

struct GenericImageTensorBuilder : public amp::TensorBuilder {

    virtual amp::Result<void> build(const TensorBuilder::Setup &setup) override;
};

} // namespace amp
