#pragma once

#include "amp/Result.h"
#include "op/Op.h"

#include <vector>

namespace amp {

class OpChain {
    std::vector<amp::OpRef> opRefs;
    std::vector<amp::Op *> opPtrs;

  public:
    amp::Result<void> setupFromFile(const std::string &jsonFile);

    void add(amp::OpRef &opRef) {
        opRefs.push_back(std::move(opRef));
    }

    amp::Result<void> bind() {
        opPtrs.resize(opRefs.size());
        for (size_t i = 0; i < opRefs.size(); i++) {
            opPtrs[i] = opRefs[i].get();
        }

        for (size_t i = 0; i < opPtrs.size(); i++) {
            opPtrs[i]->bind(i, opPtrs);
        }

        return {};
    }

    amp::Result<void> execute(amp::OpChainContext &opChainContext) {
        for (const auto &op : opPtrs) {
            auto opResult = op->process(opChainContext);
            if (!opResult) {
                return opResult;
            }
        }
        return {};
    }
};

} // namespace amp