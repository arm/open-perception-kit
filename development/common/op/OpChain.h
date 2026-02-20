/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "op/OpRef.h"

#include <vector>

namespace amp {

class OpChain {
    std::string name;
    std::vector<amp::OpRef> opRefs;
    std::vector<amp::Op *> opPtrs;

  public:
    amp::Result<void> setupFromDescriptor(const amp::OpChainDescriptor &descriptor);
    amp::Result<void> setupFromFile(const std::string &jsonFile);

    const std::string &getName();
    void add(amp::OpRef &opRef);
    amp::Result<void> bind();
    amp::Result<void> execute(amp::OpChainContext &opChainContext);
};

} // namespace amp