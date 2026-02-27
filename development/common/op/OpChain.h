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

    // validation
    amp::Result<void> validateGroupedLoopIds();
    amp::Result<void> validate();

  public:
    // creation
    amp::Result<void> setupFromDescriptor(const amp::OpChainDescriptor &descriptor);
    amp::Result<void> setupFromFile(const std::string &jsonFile);

    const std::string &getName();
    void add(amp::OpRef &opRef);

    // when all ops are added, bind them together, e.g. to let them know about each other
    amp::Result<void> bind();

    // execute the chain, e.g. for one inference
    amp::Result<void> execute(amp::OpChainContext &opChainContext);

    // debug
    std::string toString() const {
        std::string ret;
        for (size_t i = 0; i < opPtrs.size(); i++) {
            auto op = opPtrs[i];
            if (op->loopId)
                ret += fmt::format("[{}]{}/{}\n", op->loopId, op->libName, op->opName);
            else
                ret += fmt::format("{}/{}\n", op->libName, op->opName);
        }
        return ret;
    }
};

} // namespace amp