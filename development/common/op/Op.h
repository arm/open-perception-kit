#pragma once

#include <string>

#include "amp/AttributeMap.h"
#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/Tools.h"

#include "op/OpChainContext.h"

namespace amp {

struct Op {
    virtual ~Op() {}
    virtual Result<void> configure(const AttributeMap &attributes) = 0;
    virtual Result<void> process(OpChainContext &opChainContext) = 0;
};

class OpRef {
  public:
    OpRef();
    ~OpRef();

    Result<void> bind(const std::string &soName, const std::string &opName);

    OpRef(const OpRef &) = delete;
    OpRef &operator=(const OpRef &) = delete;

    OpRef(OpRef &&other) noexcept;
    OpRef &operator=(OpRef &&other) noexcept;

    amp::Op *get() const noexcept {
        return op;
    }
    amp::Op &operator*() const {
        return *op;
    }
    amp::Op *operator->() const noexcept {
        return op;
    }

  private:
    using CreateFn = void *(*)(const char *);
    using DeleteFn = void (*)(void *);

    void reset() noexcept;

    DynamicLibraryHandle dlHandle = nullptr;
    CreateFn createFn = nullptr;
    DeleteFn destroyFn = nullptr;
    amp::Op *op = nullptr;
};

} // namespace amp