/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <string>

#include "pek/AttributeMap.h"
#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/Tools.h"

#include "op/OpChainContext.h"

namespace pek::op {

struct Op;

class OpRef {
  public:
    OpRef();
    ~OpRef();

    Result<void> bind(const std::string &soName, const std::string &opName);

    OpRef(const OpRef &) = delete;
    OpRef &operator=(const OpRef &) = delete;

    OpRef(OpRef &&other) noexcept;
    OpRef &operator=(OpRef &&other) noexcept;

    pek::op::Op *get() const noexcept {
        return op;
    }
    pek::op::Op &operator*() const {
        return *op;
    }
    pek::op::Op *operator->() const noexcept {
        return op;
    }

  private:
    using CreateFn = void *(*)(const char *);
    using DeleteFn = void (*)(void *);

    void reset() noexcept;

    DynamicLibraryHandle dlHandle = nullptr;
    CreateFn createFn = nullptr;
    DeleteFn destroyFn = nullptr;
    pek::op::Op *op = nullptr;
};

} // namespace pek::op