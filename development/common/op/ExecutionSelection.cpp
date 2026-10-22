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
#include "op/ExecutionCalibration.h"
#include "op/GpuPreprocess.h"
#include "op/OpRef.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <utility>

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
        std::string inputFailure;
        bool ready{false};

        bool pending() const {
            return preparation.valid();
        }
        bool available() const {
            return failure.empty() && inputFailure.empty();
        }

        void collect(const bool wait = false) {
            if (!pending())
                return;
            if (wait)
                preparation.wait();
            if (preparation.wait_for(noWait) != std::future_status::ready)
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

    struct Preprocessor {
        Op *cpu{nullptr};
        AttributeMap attributes;
        ExecutionMode requested{ExecutionMode::Auto};
        PreprocessPath selected{PreprocessPath::Cpu};
        std::array<Variant, tensorVariantCount> variants;
        std::string sourceName;
        std::string layout;
        std::string cpuFailure;
        std::string warning;
        bool transferReported{false};
    };

    std::vector<Op *> ops;
    std::vector<Preprocessor> preprocessors;
    std::optional<perf::PipelineSample> sample;
    std::unique_ptr<ExecutionCalibration> calibration;
    uint64_t inferredFrames{0};
    uint64_t inferenceCalls{0};
    bool freshSample{false};
    bool selectedReady{false};
    bool selectionReported{false};

    static size_t variantIndex(const PreprocessPath path) {
        return path == PreprocessPath::ReadbackGpu ? readbackTensorVariant : sharedTensorVariant;
    }

    void invalidateSelection() {
        calibration.reset();
        selectedReady = false;
        selectionReported = false;
    }

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
        if (reason.empty() || preprocessor.warning == reason)
            return;
        preprocessor.warning = reason;
        log::warning("{}: GPU preprocessing unavailable: {}. Using CPU preprocessing.\n",
                     preprocessor.cpu->instanceId,
                     reason);
    }

    Result<std::vector<std::vector<PreprocessPath>>> choices() const {
        std::vector<std::vector<PreprocessPath>> options;
        for (const auto &preprocessor : preprocessors) {
            std::vector<PreprocessPath> allowed;
            if (preprocessor.requested != ExecutionMode::Gpu && preprocessor.cpuFailure.empty())
                allowed.push_back(PreprocessPath::Cpu);
            if (preprocessor.requested != ExecutionMode::Cpu &&
                (sample || preprocessor.requested == ExecutionMode::Gpu)) {
                if (preprocessor.variants[sharedTensorVariant].available())
                    allowed.push_back(PreprocessPath::SharedGpu);
                if (preprocessor.variants[readbackTensorVariant].available())
                    allowed.push_back(PreprocessPath::ReadbackGpu);
            }
            if (allowed.empty()) {
                std::string reason{preprocessor.cpuFailure};
                for (const auto &candidate : preprocessor.variants) {
                    const auto &failure{candidate.failure.empty() ? candidate.inputFailure
                                                                  : candidate.failure};
                    if (!failure.empty())
                        reason += (reason.empty() ? "" : "; ") + failure;
                }
                if (preprocessor.requested == ExecutionMode::Gpu)
                    return tl::unexpected(unavailableGpu(*preprocessor.cpu, reason).error());
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidData,
                              preprocessor.cpu->instanceId +
                                  ": no preprocessing path can consume this input: " + reason));
            }
            options.push_back(std::move(allowed));
        }
        return options;
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
    }
    return {};
}

void ExecutionSelection::observePipeline(const perf::PipelineSample &sample) {
    if (!state->sample || state->sample->generation != sample.generation ||
        state->sample->configuration != sample.configuration ||
        state->sample->endpoint != sample.endpoint)
        state->invalidateSelection();
    state->sample = sample;
    state->freshSample = true;
}

void ExecutionSelection::completeFrame(const uint64_t inferenceCalls) {
    if (inferenceCalls != 0) {
        ++state->inferredFrames;
        state->inferenceCalls += inferenceCalls;
    }
}

Result<bool> ExecutionSelection::prepareFrame(const OpChainContext &context) {
    if (state->preprocessors.empty())
        return true;
    const bool streaming{state->sample.has_value()};
    bool probing{false};
    bool forcedPending{false};
    for (auto &preprocessor : state->preprocessors) {
        const auto layout{frameLayout(context.getVideoFrame(preprocessor.sourceName))};
        if (layout != preprocessor.layout) {
            preprocessor.layout = layout;
            preprocessor.cpuFailure.clear();
            preprocessor.transferReported = false;
            for (auto &candidate : preprocessor.variants)
                candidate.inputFailure.clear();
            state->invalidateSelection();
        }
        if (preprocessor.requested == ExecutionMode::Cpu ||
            (!streaming && preprocessor.requested == ExecutionMode::Auto))
            continue;
        state->prepare(preprocessor, sharedTensorVariant);
        auto &shared{preprocessor.variants[sharedTensorVariant]};
        shared.collect(!streaming);
        bool pending{shared.pending()};
        if (!shared.failure.empty()) {
            state->prepare(preprocessor, readbackTensorVariant);
            auto &readback{preprocessor.variants[readbackTensorVariant]};
            readback.collect(!streaming);
            pending = readback.pending();
        }
        probing = probing || pending;
        forcedPending = forcedPending || (pending && preprocessor.requested == ExecutionMode::Gpu);
    }
    if (probing) {
        state->selectedReady = false;
        // Auto continues through CPU; forced stages must already have a ready GPU path.
        for (auto &preprocessor : state->preprocessors) {
            if (preprocessor.requested == ExecutionMode::Gpu) {
                const auto &shared{preprocessor.variants[sharedTensorVariant]};
                const auto &readback{preprocessor.variants[readbackTensorVariant]};
                if (shared.ready && shared.available())
                    preprocessor.selected = PreprocessPath::SharedGpu;
                else if (readback.ready && readback.available())
                    preprocessor.selected = PreprocessPath::ReadbackGpu;
                else
                    forcedPending = true;
            } else {
                preprocessor.selected = PreprocessPath::Cpu;
                forcedPending = forcedPending || !preprocessor.cpuFailure.empty();
            }
        }
        return !forcedPending;
    }

    const bool freshSample{std::exchange(state->freshSample, false)};
    if (state->calibration && state->selectedReady && freshSample)
        state->calibration->observe(*state->sample, state->inferredFrames, state->inferenceCalls);
    for (;;) {
        if (!state->calibration) {
            auto choices{state->choices()};
            if (!choices)
                return tl::unexpected(choices.error());
            state->calibration = std::make_unique<ExecutionCalibration>(std::move(*choices));
        }
        bool rejected{false};
        bool pending{false};
        const auto &combination{state->calibration->selected()};
        for (size_t index{0}; index < state->preprocessors.size(); ++index) {
            auto &preprocessor{state->preprocessors[index]};
            const auto selected{combination[index]};
            if (selected != preprocessor.selected)
                preprocessor.transferReported = false;
            preprocessor.selected = selected;
            if (selected == PreprocessPath::Cpu) {
                const auto &shared{preprocessor.variants[sharedTensorVariant]};
                const auto &readback{preprocessor.variants[readbackTensorVariant]};
                if (!shared.available() && !readback.available())
                    State::warnCpu(preprocessor,
                                   shared.failure.empty() ? shared.inputFailure : shared.failure);
                continue;
            }
            const auto variant{State::variantIndex(selected)};
            state->prepare(preprocessor, variant);
            auto &candidate{preprocessor.variants[variant]};
            candidate.collect(!streaming);
            if (!candidate.available()) {
                rejected = true;
                continue;
            }
            pending = pending || candidate.pending();
        }
        if (rejected) {
            state->invalidateSelection();
            continue;
        }
        state->selectedReady = !pending;
        if (state->calibration->finished() && !state->selectionReported) {
            log::notice(
                "Preprocessing calibration {} after {} inferred frames and {} inference calls.\n",
                state->calibration->measured() ? "selected a measured path"
                                               : "has no output measurement",
                state->inferredFrames,
                state->inferenceCalls);
            state->selectionReported = true;
        }
        return !pending;
    }
}

Result<OpSignal> ExecutionSelection::process(Op &operation, OpChainContext &context) {
    const auto found{std::ranges::find_if(
        state->preprocessors, [&operation](const auto &item) { return item.cpu == &operation; })};
    if (found == state->preprocessors.end())
        return operation.process(context);
    auto &preprocessor{*found};
    const bool gpuSelected{preprocessor.selected != PreprocessPath::Cpu};
    const auto variant{State::variantIndex(preprocessor.selected)};
    auto result{gpuSelected ? (*preprocessor.variants[variant].operation)->process(context)
                            : operation.process(context)};
    if (!result) {
        if (!state->sample || preprocessor.requested == ExecutionMode::Cpu) {
            if (preprocessor.requested == ExecutionMode::Gpu)
                return tl::unexpected(unavailableGpu(operation, result.error().toString()).error());
            return result;
        }
        if (gpuSelected) {
            preprocessor.variants[variant].inputFailure = result.error().toString();
            if (preprocessor.requested == ExecutionMode::Auto && preprocessor.cpuFailure.empty())
                State::warnCpu(preprocessor, result.error().toString());
        } else {
            preprocessor.cpuFailure = result.error().toString();
        }
        state->invalidateSelection();
        // Do not replay inference or reset stateful operations; choose again on the next frame.
        return OpSignal::AbortChain;
    }
    if (!preprocessor.transferReported && *result == OpSignal::Continue) {
        if (gpuSelected) {
            const auto *const info{
                (*preprocessor.variants[variant].operation)->as<OpInterfaceGpuPreprocess>()};
            log::notice("{}: GPU preprocessing on {}; source {}, tensor {}.\n",
                        operation.instanceId,
                        info->identity(),
                        info->inputCopied() ? "uploaded" : "shared",
                        info->tensorCopied() ? "copied" : "shared");
        } else {
            log::notice("{}: CPU preprocessing.\n", operation.instanceId);
        }
        preprocessor.transferReported = true;
    }
    return result;
}

void ExecutionSelection::destroyInstances() {
    for (auto &preprocessor : state->preprocessors) {
        for (auto &candidate : preprocessor.variants) {
            candidate.collect(true);
            if (candidate.operation)
                candidate.operation->destroyInstance();
        }
    }
}

} // namespace opk::op
