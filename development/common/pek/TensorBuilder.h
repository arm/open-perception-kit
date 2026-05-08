/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Color.h"
#include "pek/Result.h"
#include "pek/Types.h"

namespace pek {

struct TensorBuilder {

    struct Setup {
        ImageLayoutDesc imageSourceDesc;
        ImageLayoutDesc imageDestinationDesc;
    };

    virtual pek::Result<void> build(const TensorBuilder::Setup &setup) = 0;
};

} // namespace pek