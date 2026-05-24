/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "op/OpRef.h"
#include "pek/Result.h"

#include <vector>

namespace pek {

class OpChain {
    std::string name;
    std::vector<pek::OpRef> opRefs;
    std::vector<pek::Op *> opPtrs;

    // validation
    pek::Result<void> validateGroupedLoopIds();
    pek::Result<void> validateLoopGroupSizes();
    pek::Result<void> validate();

  public:
    // creation
    pek::Result<void> setupFromDescriptor(const pek::OpChainDescriptor &descriptor);
    pek::Result<void> setupFromFile(const std::string &jsonFile);

    const std::string &getName();
    void add(pek::OpRef &opRef);

    // when all ops are added, bind them together, e.g. to let them know about each other
    pek::Result<void> bind();

    // execute the chain, e.g. for one inference
    pek::Result<void> execute(pek::OpChainContext &opChainContext);

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

} // namespace pek