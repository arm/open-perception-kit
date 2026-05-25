/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pek/AttributeMap.h"
#include "pek/Model.h"
#include "pek/Result.h"

#include "op/OpChainContext.h"

namespace pek::op {

// interface for ops that can provide tensor IO information
struct OpInterfaceInference {
    virtual const pek::Model &getModel() const = 0;
    virtual uint8_t *getTensorDataAddress(size_t index) const = 0;
};

// interface for ops that do inference postprocessing
struct OpInterfacePostprocessor {
    virtual std::string getPostprocessorId() = 0;
};

struct Op {
    virtual ~Op() {}
    // called when an instance is created Op can setup itself
    virtual Result<void> configure(const AttributeMap &attributes) = 0;
    // called when the OpChain is built, here the Op can get info from the other Op instances
    virtual Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) = 0;
    // called to make the Op do its job
    virtual Result<void> process(OpChainContext &opChainContext) = 0;

    std::string libName, opName;
    std::string group;
    size_t loopId = 0;

    template <class T> T *as() noexcept {
        return dynamic_cast<T *>(this);
    }

    template <class T> const T *as() const noexcept {
        return dynamic_cast<const T *>(this);
    }
};

} // namespace pek::op
