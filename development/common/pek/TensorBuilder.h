/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/ImageOpDesc.h"
#include "pek/Result.h"

namespace pek {

struct TensorBuilder {

    struct Setup {
        ImageOpDesc imageSourceDesc;
        ImageOpDesc imageDestinationDesc;
    };

    virtual pek::Result<void> build(const TensorBuilder::Setup &setup) = 0;
};

} // namespace pek