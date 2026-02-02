#pragma once

#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "op/OpRef.h"

#include <vector>

namespace amp {

class OpChain {
    std::vector<amp::OpRef> opRefs;
    std::vector<amp::Op *> opPtrs;

  public:
    amp::Result<void> setupFromDescriptor(const amp::OpChainDescriptor &descriptor);
    amp::Result<void> setupFromFile(const std::string &jsonFile);

    void add(amp::OpRef &opRef);
    amp::Result<void> bind();
    amp::Result<void> execute(amp::OpChainContext &opChainContext);
};

} // namespace amp