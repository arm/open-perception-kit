/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"
#include "op/OpSetupContext.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

namespace {

class BlockingSetupOp final : public pek::op::Op {
  public:
    pek::Result<void> configure(const pek::AttributeMap &attributes,
                                pek::op::OpSetupContext &setupContext) override {
        if (attributes.contains("modelDescriptor")) {
            std::string modelDescriptorPath;
            try {
                modelDescriptorPath = attributes.getString("modelDescriptor");
            } catch (const pek::AttributeError &error) {
                return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData, error.what())};
            }

            auto descriptor = setupContext.resolveModelDescriptor(modelDescriptorPath);
            if (!descriptor)
                return tl::unexpected{descriptor.error()};
        }

        const char *startedPath = std::getenv("PEK_TEST_BLOCKING_SETUP_STARTED");
        const char *releasePath = std::getenv("PEK_TEST_BLOCKING_SETUP_RELEASE");
        if (startedPath == nullptr || releasePath == nullptr) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          "Blocking setup test operation requires its synchronization paths")};
        }

        std::ofstream started(startedPath);
        if (!started) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::FileOperationError,
                          "Blocking setup test operation could not publish its started marker")};
        }
        started << "started\n";
        started.close();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code error;
            if (std::filesystem::exists(releasePath, error))
                return {};
            if (error) {
                return tl::unexpected{PEK_ERROR(
                    pek::ErrorFlag::FileOperationError,
                    "Blocking setup test operation could not inspect its release marker")};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::SystemFailure, "Blocking setup test operation timed out")};
    }

    pek::Result<void> bind(size_t, const std::vector<pek::op::Op *> &) override {
        return {};
    }

    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &) override {
        return pek::op::OpSignal::Continue;
    }
};

} // namespace

extern "C" void pek_delete_op_instance(void *instance) {
    delete static_cast<pek::op::Op *>(instance);
}

extern "C" void *pek_create_op_instance(const char *opName) {
    if (opName == nullptr || std::strcmp(opName, "BlockingSetup") != 0)
        return nullptr;
    return new BlockingSetupOp();
}
