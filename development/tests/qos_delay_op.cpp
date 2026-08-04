/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

namespace {

// A 30 FPS frame has a roughly 33 ms budget. Sleeping for 50 ms makes one following
// frame become stale, exercising pekinfer's proactive skip policy without loading a model.
class DelayOp final : public pek::op::Op {
  public:
    pek::Result<void> configure(const pek::AttributeMap &) override {
        return {};
    }

    pek::Result<void> bind(size_t, const std::vector<pek::op::Op *> &) override {
        return {};
    }

    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return pek::op::OpSignal::Continue;
    }
};

} // namespace

extern "C" void pek_delete_op_instance(void *instance) {
    std::unique_ptr<pek::op::Op> owner( // NOSONAR: adopt the instance returned by the plugin ABI.
        static_cast<pek::op::Op *>(instance));
}

extern "C" void *pek_create_op_instance(const char *opName) {
    if (opName == nullptr || std::strcmp(opName, "Delay") != 0)
        return nullptr;
    return std::make_unique<DelayOp>().release();
}
