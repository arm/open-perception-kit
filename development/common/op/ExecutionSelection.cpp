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
#include <array>
#include <chrono>
#include <future>
#include <optional>

namespace opk::op {
namespace {
constexpr std::string_view gpuLibrary{"opk-gpu-ops"};
constexpr std::string_view imagePreprocess{"GenericImagePreprocess"};
constexpr size_t sharedTensorVariant{0};
constexpr size_t readbackTensorVariant{1};
constexpr size_t tensorVariantCount{2};
constexpr auto noWait{std::chrono::seconds::zero()};

Result<void> unavailableGpu(const Op &operation, const std::string &reason) {
    return tl::unexpected(
        OPK_ERROR(ErrorFlag::InvalidOpChain,
                  fmt::format("{}: GPU execution was requested but is unavailable: {}. "
                              "Use execution=auto to allow CPU fallback, or execution=cpu.",
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

std::string frameLayout(const mediaio::VideoFrame *const frame) {
    if (!frame)
        return {};
    nlohmann::json layout{{"width", frame->width()},
                          {"height", frame->height()},
                          {"format", static_cast<int>(frame->format())},
                          {"memory", static_cast<int>(frame->memoryType())},
                          {"matrix", static_cast<int>(frame->yuvColorMatrix())},
                          {"range", static_cast<int>(frame->yuvRange())},
                          {"planes", nlohmann::json::array()}};
    for (const auto &plane : frame->planes())
        layout["planes"].push_back({plane.byteSize(), plane.strideBytes(), plane.offset()});
    return layout.dump();
}
} // namespace

struct ExecutionSelection::State {
    struct Variant {
        std::unique_ptr<OpRef> operation;
        std::future<Result<void>> preparation;
        std::string failure;
        bool ready{false};

        bool pending() const {
            return preparation.valid();
        }

        void collect() {
            if (!pending() || preparation.wait_for(noWait) != std::future_status::ready)
                return;
            try {
                auto result = preparation.get();
                if (result)
                    ready = true;
                else
                    failure = result.error().toString();
            } catch (const std::exception &error) {
                failure = error.what();
            }
        }
    };

    struct Preprocessor {
        Op *cpu{nullptr};
        AttributeMap attributes;
        ExecutionMode requested{ExecutionMode::Auto};
        bool useGpu{false};
        size_t tensorVariant{sharedTensorVariant};
        std::array<Variant, tensorVariantCount> variants;
        std::string sourceName;
        std::string layout;
        std::string warning;
        bool inputRejected{false};
        bool transferReported{false};
    };

    std::vector<Op *> ops;
    std::vector<Preprocessor> preprocessors;

    void prepare(Preprocessor &preprocessor, const size_t variant) {
        auto &candidate{preprocessor.variants[variant]};
        if (candidate.operation || candidate.pending() || !candidate.failure.empty())
            return;
        const auto *const cpu{preprocessor.cpu};
        auto operation{std::make_unique<OpRef>()};
        auto result{operation->bind(std::string(gpuLibrary), std::string(imagePreprocess))};
        if (!result) {
            candidate.failure = result.error().toString();
            return;
        }
        auto *const gpu{(*operation)->as<OpInterfaceGpuPreprocess>()};
        if (!gpu) {
            candidate.failure = "GPU preprocessing module has no device interface";
            return;
        }
        (*operation)->libName = std::string(gpuLibrary);
        (*operation)->opName = std::string(imagePreprocess);
        (*operation)->instanceId = cpu->instanceId;
        (*operation)->index = cpu->index;
        (*operation)->loopId = cpu->loopId;
        gpu->requestTensorReadback(variant == readbackTensorVariant);
        result = (*operation)->configure(preprocessor.attributes);
        if (result)
            result = (*operation)->bind(cpu->index, ops);
        if (!result) {
            candidate.failure = result.error().toString();
            return;
        }
        candidate.operation = std::move(operation);
        try {
            candidate.preparation =
                std::async(std::launch::async, [gpu] { return gpu->prepareDevice(); });
        } catch (const std::exception &error) {
            candidate.failure = error.what();
        }
    }

    static void warnCpu(Preprocessor &preprocessor, const std::string &reason) {
        if (preprocessor.warning == reason)
            return;
        preprocessor.warning = reason;
        log::warning("{}: GPU preprocessing unavailable: {}. Using CPU preprocessing.\n",
                     preprocessor.cpu->instanceId,
                     reason);
    }
};

ExecutionSelection::ExecutionSelection() : state{std::make_unique<State>()} {}
ExecutionSelection::~ExecutionSelection() = default;

Result<void> ExecutionSelection::configure(const OpChainDescriptor &descriptor,
                                           const std::vector<Op *> &ops) {
    state->ops = ops;
    const auto defaults{descriptor.execution.value_or(ExecutionDefaults{})};
    for (size_t index{0}; index < ops.size(); ++index) {
        auto &operation{*ops[index]};
        const auto stage{stageOf(operation)};
        const auto mode{descriptor.ops[index].execution.value_or(stage ? defaults.mode(*stage)
                                                                       : ExecutionMode::Auto)};
        const bool selectablePreprocess{operation.libName == "opk-std-ops" &&
                                        operation.opName == imagePreprocess};
        if (!selectablePreprocess) {
            const bool alreadyGpu{operation.as<OpInterfaceGpuPreprocess>() != nullptr};
            if (mode == ExecutionMode::Gpu && !alreadyGpu)
                return unavailableGpu(operation, "this Op has only a CPU implementation");
            if (mode == ExecutionMode::Cpu && alreadyGpu)
                return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain,
                                                operation.instanceId +
                                                    ": this GPU Op cannot honor execution=cpu; "
                                                    "use opk-std-ops/GenericImagePreprocess"));
            if (stage && mode == ExecutionMode::Auto && !alreadyGpu)
                log::warning("{}: no GPU implementation is available; using CPU execution.\n",
                             operation.instanceId);
            continue;
        }
        State::Preprocessor preprocessor;
        preprocessor.cpu = &operation;
        preprocessor.attributes = descriptor.ops[index].attributes;
        preprocessor.requested = mode;
        preprocessor.sourceName = preprocessor.attributes.getStringOrDefault("inputImageSourceName",
                                                                             "pipelineVideoFrame");
        state->preprocessors.push_back(std::move(preprocessor));
        if (mode != ExecutionMode::Cpu)
            state->prepare(state->preprocessors.back(), sharedTensorVariant);
    }
    return {};
}

Result<bool> ExecutionSelection::prepareFrame(const OpChainContext &context) {
    bool ready = true;
    for (auto &preprocessor : state->preprocessors) {
        const auto layout{frameLayout(context.getVideoFrame(preprocessor.sourceName))};
        if (layout != preprocessor.layout) {
            preprocessor.layout = layout;
            preprocessor.inputRejected = false;
            preprocessor.transferReported = false;
        }
        if (preprocessor.requested == ExecutionMode::Cpu)
            continue;
        auto &candidate = preprocessor.variants[preprocessor.tensorVariant];
        candidate.collect();
        if (!candidate.failure.empty()) {
            if (preprocessor.requested == ExecutionMode::Gpu)
                return tl::unexpected(unavailableGpu(*preprocessor.cpu, candidate.failure).error());
            State::warnCpu(preprocessor, candidate.failure);
            preprocessor.useGpu = false;
        } else if (candidate.ready && !preprocessor.inputRejected) {
            preprocessor.useGpu = true;
        } else if (candidate.pending() && preprocessor.requested == ExecutionMode::Gpu) {
            ready = false;
        }
    }
    return ready;
}

Result<OpSignal> ExecutionSelection::process(Op &operation, OpChainContext &context) {
    const auto found{std::ranges::find_if(
        state->preprocessors, [&operation](const auto &item) { return item.cpu == &operation; })};
    if (found == state->preprocessors.end() || !found->useGpu)
        return operation.process(context);
    auto &preprocessor{*found};
    auto &gpu{preprocessor.variants[preprocessor.tensorVariant].operation};
    auto result{(*gpu)->process(context)};
    if (!result) {
        if (preprocessor.requested == ExecutionMode::Gpu)
            return tl::unexpected(unavailableGpu(operation, result.error().toString()).error());
        preprocessor.inputRejected = true;
        preprocessor.useGpu = false;
        State::warnCpu(preprocessor, result.error().toString());
        return operation.process(context);
    }
    if (!preprocessor.transferReported && *result == OpSignal::Continue) {
        const auto *const info = (*gpu)->as<OpInterfaceGpuPreprocess>();
        log::notice("{}: GPU preprocessing on {}; source {}, tensor {}.\n",
                    operation.instanceId,
                    info->identity(),
                    info->inputCopied() ? "uploaded" : "shared",
                    info->tensorCopied() ? "copied" : "shared");
        preprocessor.transferReported = true;
    }
    return result;
}

void ExecutionSelection::destroyInstances() {
    for (auto &preprocessor : state->preprocessors) {
        for (auto &candidate : preprocessor.variants) {
            if (candidate.pending())
                candidate.preparation.wait();
            candidate.collect();
            if (candidate.operation)
                candidate.operation->destroyInstance();
        }
    }
}

} // namespace opk::op
