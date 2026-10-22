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

#include "Log.h"
#include "op/GpuPreprocess.h"
#include "op/OpRef.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <optional>

namespace opk::op {
namespace {
constexpr std::string_view gpuLibrary{"opk-gpu-ops"};
constexpr std::string_view imagePreprocess{"GenericImagePreprocess"};
constexpr auto noWait{std::chrono::seconds::zero()};

Result<void> unavailableGpu(const Op &operation, const std::string &reason) {
    return tl::unexpected(
        OPK_ERROR(ErrorFlag::InvalidOpChain,
                  fmt::format("{}: GPU execution was requested but is unavailable: {}. "
                              "Use execution=cpu.",
                              operation.instanceId,
                              reason)));
}

std::optional<ExecutionStage> stageOf(const Op &operation) {
    if ((operation.libName == "opk-std-ops" && operation.opName == imagePreprocess) ||
        operation.as<OpInterfaceGpuPreprocess>())
        return ExecutionStage::Preprocessing;
    if (operation.as<OpInterfaceInference>())
        return ExecutionStage::Inference;
    if (operation.as<OpInterfacePostprocessor>())
        return ExecutionStage::Postprocessing;
    return std::nullopt;
}
} // namespace

struct ExecutionSelection::State {
    struct GpuOperation {
        Op *original{nullptr};
        std::unique_ptr<OpRef> replacement;
        Op *operation{nullptr};
        std::future<Result<void>> preparation;
        std::string failure;
        bool ready{false};
        bool transferReported{false};

        void collect() {
            if (!preparation.valid() || preparation.wait_for(noWait) != std::future_status::ready)
                return;
            try {
                const auto result{preparation.get()};
                if (result)
                    ready = true;
                else
                    failure = result.error().toString();
            } catch (const std::exception &error) {
                failure = error.what();
            }
        }
    };

    std::vector<GpuOperation> gpuOperations;
};

ExecutionSelection::ExecutionSelection() : state{std::make_unique<State>()} {}
ExecutionSelection::~ExecutionSelection() = default;

Result<void> ExecutionSelection::configure(const OpChainDescriptor &descriptor,
                                           const std::vector<Op *> &ops) {
    const auto defaults{descriptor.execution.value_or(ExecutionDefaults{})};
    for (size_t index{0}; index < ops.size(); ++index) {
        auto &operation{*ops[index]};
        const auto stage{stageOf(operation)};
        const auto mode{descriptor.ops[index].execution.value_or(stage ? defaults.mode(*stage)
                                                                       : ExecutionMode::Cpu)};
        const bool alreadyGpu{operation.as<OpInterfaceGpuPreprocess>() != nullptr};
        if (mode == ExecutionMode::Cpu) {
            if (alreadyGpu)
                return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain,
                                                operation.instanceId +
                                                    ": this GPU Op cannot honor execution=cpu; "
                                                    "use opk-std-ops/GenericImagePreprocess"));
            continue;
        }
        const bool selectablePreprocess{operation.libName == "opk-std-ops" &&
                                        operation.opName == imagePreprocess};
        if (!selectablePreprocess && !alreadyGpu)
            return unavailableGpu(operation, "this Op has only a CPU implementation");

        State::GpuOperation selected;
        selected.original = &operation;
        selected.operation = &operation;
        if (selectablePreprocess) {
            selected.replacement = std::make_unique<OpRef>();
            auto result{
                selected.replacement->bind(std::string(gpuLibrary), std::string(imagePreprocess))};
            if (!result)
                return unavailableGpu(operation, result.error().toString());
            selected.operation = selected.replacement->get();
            selected.operation->libName = std::string(gpuLibrary);
            selected.operation->opName = std::string(imagePreprocess);
            selected.operation->instanceId = operation.instanceId;
            selected.operation->index = operation.index;
            selected.operation->loopId = operation.loopId;
            result = selected.operation->configure(descriptor.ops[index].attributes);
            if (result)
                result = selected.operation->bind(index, ops);
            if (!result)
                return unavailableGpu(operation, result.error().toString());
        }
        auto *const gpu{selected.operation->as<OpInterfaceGpuPreprocess>()};
        if (!gpu)
            return unavailableGpu(operation, "GPU preprocessing module has no device interface");
        try {
            selected.preparation =
                std::async(std::launch::async, [gpu] { return gpu->prepareDevice(); });
        } catch (const std::exception &error) {
            return unavailableGpu(operation, error.what());
        }
        state->gpuOperations.push_back(std::move(selected));
    }
    return {};
}

Result<bool> ExecutionSelection::prepareFrame() {
    bool ready{true};
    for (auto &selected : state->gpuOperations) {
        selected.collect();
        if (!selected.failure.empty())
            return tl::unexpected(unavailableGpu(*selected.original, selected.failure).error());
        ready = ready && selected.ready;
    }
    return ready;
}

Result<OpSignal> ExecutionSelection::process(Op &operation, OpChainContext &context) {
    const auto found{std::ranges::find_if(state->gpuOperations, [&operation](const auto &item) {
        return item.original == &operation;
    })};
    if (found == state->gpuOperations.end())
        return operation.process(context);
    auto &selected{*found};
    const auto result{selected.operation->process(context)};
    if (!result)
        return tl::unexpected(unavailableGpu(operation, result.error().toString()).error());
    if (!selected.transferReported && *result == OpSignal::Continue) {
        const auto *const info{selected.operation->as<OpInterfaceGpuPreprocess>()};
        log::notice("{}: GPU preprocessing on {}; source {}, tensor {}.\n",
                    operation.instanceId,
                    info->identity(),
                    info->inputCopied() ? "uploaded" : "shared",
                    info->tensorCopied() ? "copied" : "shared");
        selected.transferReported = true;
    }
    return result;
}

void ExecutionSelection::destroyInstances() {
    for (auto &selected : state->gpuOperations) {
        if (selected.preparation.valid())
            selected.preparation.wait();
        selected.collect();
        if (selected.replacement)
            selected.replacement->destroyInstance();
    }
}

} // namespace opk::op
