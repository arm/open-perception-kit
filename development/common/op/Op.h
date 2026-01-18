#pragma once

#include "amp/AttributeMap.h"
#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/Tools.h"

namespace amp {

struct Op {

    virtual ~Op() {}

    virtual Result<void> configure(const AttributeMap &attributes) = 0;
    virtual Result<void> process(PerceptionContext &perceptionContext) = 0;
};

// ---

// embeds a pointer to an Op instance, use for RAII
class OpRef {

  public:
    OpRef(Op *op);
    virtual ~OpRef();

  private:
    Op *op;
    DynamicLibraryHandle dlHandle = nullptr;
};

} // namespace amp