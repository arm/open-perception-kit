/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "op/ExecutionSelection.h"

#include "op/GpuPreprocess.h"

#include <chrono>
#include <future>

namespace opk::op {
namespace {
constexpr std::string_view standardLibrary{"opk-std-ops"};
constexpr std::string_view gpuLibrary{"opk-gpu-ops"};
constexpr std::string_view imagePreprocess{"GenericImagePreprocess"};
constexpr auto noWait{std::chrono::seconds::zero()};

Error unavailableGpu(const std::string &instanceId, const std::string &reason) {
    return OPK_ERROR(ErrorFlag::InvalidOpChain,
                     fmt::format("{}: GPU execution was requested but is unavailable: {}. "
                                 "Use execution.preprocessing=cpu.",
                                 instanceId,
                                 reason));
}
} // namespace

struct ExecutionSelection::GpuOperation {
    Op &operation;
    std::shared_future<Result<void>> preparation;
};

ExecutionSelection::ExecutionSelection(const ExecutionMode preprocessingMode)
    : preprocessingMode{preprocessingMode} {}

ExecutionSelection::~ExecutionSelection() {
    // Setup borrows the Ops, so finish it before the chain destroys any instances.
    for (const auto &gpuOperation : gpuOperations)
        gpuOperation.preparation.wait();
}

Result<OpRef> ExecutionSelection::loadOperation(const std::string &libraryName,
                                                const std::string &opName,
                                                const std::string &instanceId) const {
    const bool useGpu{preprocessingMode == ExecutionMode::Gpu && libraryName == standardLibrary &&
                      opName == imagePreprocess};
    const std::string selectedLibrary{useGpu ? std::string(gpuLibrary) : libraryName};
    OpRef reference;
    const auto result{reference.bind(selectedLibrary, opName)};
    if (!result) {
        if (selectedLibrary == gpuLibrary)
            return tl::unexpected(unavailableGpu(instanceId, result.error().toString()));
        return tl::unexpected(result.error());
    }
    reference->libName = selectedLibrary;
    reference->opName = opName;
    reference->instanceId = instanceId;
    if (useGpu && !reference->as<OpInterfaceGpuPreprocess>())
        return tl::unexpected(
            unavailableGpu(instanceId, "GPU preprocessing module has no device interface"));
    return reference;
}

Error ExecutionSelection::operationError(const Op &operation, const Error &error) {
    return operation.as<OpInterfaceGpuPreprocess>()
               ? unavailableGpu(operation.instanceId, error.toString())
               : error;
}

Result<void> ExecutionSelection::prepareDevices(const std::vector<Op *> &ops) {
    for (auto *const operation : ops) {
        auto *const gpu{operation->as<OpInterfaceGpuPreprocess>()};
        if (preprocessingMode == ExecutionMode::Cpu) {
            if (gpu)
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidOpChain,
                              operation->instanceId +
                                  ": this GPU Op cannot honor execution.preprocessing=cpu; "
                                  "use opk-std-ops/GenericImagePreprocess"));
            continue;
        }
        if (!gpu) {
            if (operation->as<OpInterfaceAudioPreprocessor>())
                return tl::unexpected(
                    unavailableGpu(operation->instanceId, "this Op has only a CPU implementation"));
            continue;
        }
        try {
            gpuOperations.push_back({*operation, std::async(std::launch::async, [gpu] {
                                         return gpu->prepareDevice();
                                     })});
        } catch (const std::exception &error) {
            return tl::unexpected(unavailableGpu(operation->instanceId, error.what()));
        }
    }
    return {};
}

Result<bool> ExecutionSelection::prepareFrame(const bool waitForDeviceSetup) {
    bool ready{true};
    for (const auto &gpuOperation : gpuOperations) {
        if (waitForDeviceSetup)
            gpuOperation.preparation.wait();
        if (gpuOperation.preparation.wait_for(noWait) != std::future_status::ready) {
            ready = false;
            continue;
        }
        try {
            const auto &result{gpuOperation.preparation.get()};
            if (!result)
                return tl::unexpected(operationError(gpuOperation.operation, result.error()));
        } catch (const std::exception &error) {
            return tl::unexpected(unavailableGpu(gpuOperation.operation.instanceId, error.what()));
        }
        gpuOperation.operation.as<OpInterfaceGpuPreprocess>()->beginFrame();
    }
    return ready;
}

} // namespace opk::op
