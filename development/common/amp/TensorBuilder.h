/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Color.h"
#include "amp/Result.h"
#include "amp/Types.h"

namespace amp {

struct TensorBuilder {

    struct Setup {
        ImageLayoutDesc imageSourceDesc;
        ImageLayoutDesc imageDestinationDesc;
    };

    virtual amp::Result<void> build(const TensorBuilder::Setup &setup) = 0;
};

} // namespace amp