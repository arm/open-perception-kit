/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "op/OpChain.h"

#include "Log.h"
#include "Validator.h"
#include "opk/String.h"
#include "tools.h"

#include "op/ExecutionSelection.h"
#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
using namespace opk::op;

namespace {

void clearInferenceOutputs(OpChainContext &context) {
    context.inferencePrompt.reset();
    context.inferenceOutputTensorCount = 0;
    context.inferenceOutputTensors = {};
}

} // namespace

OpChain::OpChain() = default;

OpChain::~OpChain() {
    clear();
}

void OpChain::clear() {
    executionSelection.reset();
    for (auto &reference : opRefs) {
        reference.releaseInstance();
    }
    opPtrs.clear();
    opRefs.clear();
}

const std::string &OpChain::getName() const {
    return this->name;
}

const std::string &OpChain::getDisplayName() const {
    return this->displayName;
}

const std::string &OpChain::getTask() const {
    return this->task;
}

const std::string &OpChain::getRuntime() const {
    return this->runtime;
}

std::vector<PromptConsumerCapability>
OpChain::promptCapabilities(const std::string_view instanceId,
                            const std::string_view sourceName) const {
    std::vector<PromptConsumerCapability> result;
    for (const auto *const operation : opPtrs) {
        const auto *const consumer = operation->as<OpInterfacePromptConsumer>();
        if (!consumer) {
            continue;
        }
        for (const auto &capability : consumer->promptCapabilities()) {
            if (consumer->acceptsPrompt(capability.format, instanceId, sourceName) &&
                std::find(result.begin(), result.end(), capability) == result.end()) {
                result.push_back(capability);
            }
        }
    }
    return result;
}

bool OpChain::acceptsPrompt(const opk::PromptFormat format,
                            const std::string_view instanceId,
                            const std::string_view sourceName) const {
    return std::ranges::any_of(opPtrs, [format, instanceId, sourceName](const auto *const op) {
        const auto *const consumer = op->template as<OpInterfacePromptConsumer>();
        return consumer && consumer->acceptsPrompt(format, instanceId, sourceName);
    });
}

namespace {

opk::Result<size_t> audioInputWindow(const opk::ModelInput &input,
                                     const OpInterfaceAudioPreprocessor *const preprocessor) {
    const auto &shape = input.shape;
    const size_t featureDimensions = input.dataKind == opk::DataKind::AudioPcm ? 1 : 2;
    if (shape.rank < featureDimensions || shape.rank > 8)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "Audio requires a fixed tensor with all feature dimensions"));
    for (size_t i = 0; i < shape.rank; ++i) {
        if (shape.dims[i] <= 0 || (i + featureDimensions < shape.rank && shape.dims[i] != 1))
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                          "Audio requires positive feature dimensions and singleton "
                          "batch/channel dimensions"));
    }
    size_t samples = preprocessor ? preprocessor->getRequiredAudioSamples() : 0;
    if (!preprocessor && input.dataKind == opk::DataKind::AudioPcm)
        samples = static_cast<size_t>(shape.dims[shape.rank - 1]);
    if (!samples)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "Audio requires an adjacent preprocessor with a "
                                        "positive PCM window"));
    return samples;
}

} // namespace

std::vector<opk::OutputReference>
OpChain::getProvidedOutputs(const std::string_view inferId) const {
    std::vector<opk::OutputReference> result;
    for (const auto *op : opPtrs) {
        if (const auto *producer = op->as<OpInterfacePostprocessor>()) {
            for (const auto &output : producer->getProvidedOutputs()) {
                const opk::OutputReference reference{
                    output.payloadType, opk::producerInstanceId(inferId, op->instanceId)};
                if (!output.payloadType.empty() &&
                    std::ranges::find(result, reference) == result.end())
                    result.push_back(reference);
            }
        }
    }
    return result;
}

std::vector<opk::InputBinding>
OpChain::getRequiredInputs(const std::string_view inferId,
                           const std::vector<opk::InputBinding> &bindings) const {
    std::vector<opk::InputBinding> result;
    for (const auto *op : opPtrs) {
        if (const auto *consumer = op->as<OpInterfaceResultConsumer>()) {
            for (const auto &input : consumer->getRequiredInputs()) {
                if (input.sourceOp.empty())
                    result.push_back(
                        {op->instanceId,
                         input.inputId,
                         input.payloadType,
                         opk::resolveInputProducerId(input, op->instanceId, inferId, bindings)});
            }
        }
    }
    return result;
}

opk::Result<void>
OpChain::validateInputBindings(const std::vector<opk::InputBinding> &bindings) const {
    std::vector<std::pair<std::string, std::string>> seen;
    for (const auto &binding : bindings) {
        const auto key = std::pair{binding.consumerId, binding.inputId};
        if (std::ranges::find(seen, key) != seen.end())
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, "Duplicate input connection"));
        seen.push_back(key);
        bool declared = false;
        for (const auto *op : opPtrs) {
            if (op->instanceId != binding.consumerId)
                continue;
            if (const auto *consumer = op->as<OpInterfaceResultConsumer>()) {
                for (const auto &input : consumer->getRequiredInputs())
                    declared =
                        declared || (input.sourceOp.empty() && input.inputId == binding.inputId &&
                                     input.payloadType == binding.payloadType);
            }
        }
        if (!declared)
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "Input connection does not name a declared external input"));
    }
    return {};
}

opk::Result<size_t> OpChain::getRequiredAudioSamples() const {
    size_t required = 0;
    for (size_t index = 0; index < opPtrs.size(); ++index) {
        const auto *const inference = opPtrs[index]->as<OpInterfaceInference>();
        if (!inference)
            continue;
        const auto *const preprocessor =
            index > 0 ? opPtrs[index - 1]->as<OpInterfaceAudioPreprocessor>() : nullptr;
        for (const auto &input : inference->getModel().inputs) {
            if (input.dataKind != opk::DataKind::AudioPcm &&
                input.dataKind != opk::DataKind::AudioLogMel)
                continue;
            const auto samples = audioInputWindow(input, preprocessor);
            if (!samples)
                return tl::unexpected(samples.error());
            if (required && required != *samples)
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                              "Audio inputs in one OpChain must require the same PCM window size"));
            required = *samples;
        }
    }
    // The audio scheduler advances windows between executions of the whole chain.
    // An inner Op loop would keep reusing the same window instead.
    if (required && std::ranges::any_of(opPtrs, [](const Op *op) { return op->loopId != 0; }))
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "Audio scheduling requires an unlooped OpChain; remove loopId from its Ops"));
    return required;
}

opk::Result<void> OpChain::setupFromDescriptor(const opk::op::OpChainDescriptor &descriptor) {
    if (const auto validation = opk::config::validateOpChainSemantics(descriptor); !validation.ok())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain, validation.toText()));

    clear();
    name = descriptor.name;
    displayName = descriptor.displayName;
    task = descriptor.task;
    runtime = descriptor.runtime;
    std::unordered_map<std::string, size_t> occurrences;
    executionSelection =
        std::make_unique<ExecutionSelection>(descriptor.execution.value_or(ExecutionDefaults{})
                                                 .preprocessing.value_or(ExecutionMode::Cpu));

    for (size_t opIndex = 0; opIndex < descriptor.ops.size(); ++opIndex) {
        const auto &op = descriptor.ops[opIndex];

        if (opk::utf8::count(op.id, '/') != 1) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("Op id must be library/op, but found: [{}]", op.id)));
        }

        const std::string libName = opk::utf8::split(op.id, "/")[0];
        const std::string opName = opk::utf8::split(op.id, "/")[1];
        auto &occurrenceCount = occurrences[op.id];
        const size_t occurrence = occurrenceCount;
        ++occurrenceCount;
        const std::string instanceId =
            op.instanceId.empty() ? makeDefaultInstanceId(op.id, occurrence) : op.instanceId;

        auto loadedOperation = executionSelection->loadOperation(libName, opName, instanceId);
        if (!loadedOperation)
            return tl::unexpected(loadedOperation.error());
        auto &opRef = *loadedOperation;
        opRef->index = opIndex;
        opRef->loopId = op.loopId.value_or(0);

        auto configureResult = opRef->configure(op.attributes);
        if (!configureResult) {
            return tl::unexpected(
                ExecutionSelection::operationError(*opRef, configureResult.error()));
        }

        add(opRef);
    }

    auto chainBindResult = bind();
    if (!chainBindResult) {
        return tl::unexpected(std::move(chainBindResult.error()));
    }

    if (const auto result = executionSelection->prepareDevices(opPtrs); !result)
        return result;

    opk::log::info("{}", opk::log::tools::enframe(this->toString(), "OpChain"));
    opk::log::notice("OpChain is valid\n");

    return {};
}

opk::Result<void> OpChain::setupFromFile(const std::string &filePath) {
    opk::log::info("Loading OpChain from file: [{}]\n", filePath);
    auto descResult = opk::op::OpChainDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected(std::move(descResult.error()));
    }

    return setupFromDescriptor(*descResult);
}

void OpChain::add(opk::op::OpRef &opRef) {
    opRefs.push_back(std::move(opRef));
}

opk::Result<void> OpChain::bind() {
    opPtrs.resize(opRefs.size());
    std::unordered_map<std::string, size_t> occurrences;
    std::vector<std::string> identities;
    for (size_t i = 0; i < opRefs.size(); i++) {
        opPtrs[i] = opRefs[i].get();
        const auto component = opPtrs[i]->libName + "/" + opPtrs[i]->opName;
        const auto occurrence = occurrences[component]++;
        if (opPtrs[i]->instanceId.empty())
            opPtrs[i]->instanceId = makeDefaultInstanceId(component, occurrence);
        if (std::ranges::find(identities, opPtrs[i]->instanceId) != identities.end())
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "Duplicate operation instance ID"));
        identities.push_back(opPtrs[i]->instanceId);
    }

    for (size_t i = 0; i < opPtrs.size(); i++) {
        if (const auto *producer = opPtrs[i]->as<OpInterfacePostprocessor>()) {
            for (const auto &output : producer->getProvidedOutputs())
                if (!opk::isKnownPayloadType(output.payloadType))
                    return tl::unexpected(
                        OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                  "Output must name a generated SDK payload type"));
        }
        auto opBindResult = opPtrs[i]->bind(i, opPtrs);
        if (!opBindResult) {
            return tl::unexpected(
                ExecutionSelection::operationError(*opPtrs[i], opBindResult.error()));
        }
        if (const auto *consumer = opPtrs[i]->as<OpInterfaceResultConsumer>()) {
            std::vector<std::string> inputs;
            for (const auto &input : consumer->getRequiredInputs()) {
                if (input.inputId.empty() || !opk::isKnownPayloadType(input.payloadType) ||
                    std::ranges::find(inputs, input.inputId) != inputs.end() ||
                    (!input.sourceOp.empty() && !input.producerId.empty()))
                    return tl::unexpected(
                        OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "Invalid input reference"));
                inputs.push_back(input.inputId);
                if (input.sourceOp.empty())
                    continue;
                bool found = false;
                for (size_t upstream = 0; upstream < i; ++upstream) {
                    if (opPtrs[upstream]->instanceId != input.sourceOp)
                        continue;
                    if (const auto *producer = opPtrs[upstream]->as<OpInterfacePostprocessor>()) {
                        for (const auto &output : producer->getProvidedOutputs())
                            found = found || output.payloadType == input.payloadType;
                    }
                }
                if (!found)
                    return tl::unexpected(OPK_ERROR(
                        opk::ErrorFlag::InvalidOpChain,
                        "Local input must name an earlier producer of the required payload type"));
            }
        }
    }

    return {};
}

opk::Result<void> OpChain::execute(opk::op::OpChainContext &opChainContext,
                                   const bool waitForDeviceSetup) {
    opChainContext.preparedPrompt.reset();
    opChainContext.inferencePrompt.reset();
    const auto metricName = name.empty() ? std::string("opchain") : fmt::format("opchain/{}", name);
    OPK_PERF_SCOPE(metricName);

    if (executionSelection) {
        const auto ready{executionSelection->prepareFrame(waitForDeviceSetup)};
        if (!ready)
            return tl::unexpected(ready.error());
        if (!*ready)
            return {};
    }

    size_t currentIndex = 0;
    opChainContext.resetVideoFrameIds();

    auto firstWorkerIndexForLoop = [this](size_t index, size_t loopId) {
        size_t firstGroupIndex = index;
        while (firstGroupIndex > 0 && opPtrs[firstGroupIndex - 1]->loopId == loopId) {
            --firstGroupIndex;
        }

        // Skip the controller op. Loop groups are validated to have at least two ops.
        return firstGroupIndex + 1;
    };

    auto nextIndexAfterLoop = [this](size_t index, size_t loopId) {
        while (index < opPtrs.size() && opPtrs[index]->loopId == loopId) {
            ++index;
        }
        return index;
    };

    while (currentIndex < opPtrs.size()) {
        Op *const op = opPtrs[currentIndex];
        const size_t loopId = op->loopId;

        const bool inference = op->as<OpInterfaceInference>() != nullptr;
        std::shared_ptr<const opk::PromptSnapshot> prepared;
        if (inference) {
            prepared = std::exchange(opChainContext.preparedPrompt, nullptr);
            clearInferenceOutputs(opChainContext);
            if (prepared) {
                const auto *const destination = prepared->destination();
                if (!destination || destination->inference_instance != op->instanceId) {
                    const auto error =
                        OPK_ERROR(opk::ErrorFlag::InvalidData,
                                  "Prepared prompt targets another inference operation");
                    return tl::unexpected(ExecutionSelection::operationError(*op, error));
                }
            }
        }

        const auto result{op->process(opChainContext)};
        if (!result) {
            opChainContext.preparedPrompt.reset();
            clearInferenceOutputs(opChainContext);
            return tl::unexpected(ExecutionSelection::operationError(*op, result.error()));
        }
        if (inference && *result == OpSignal::Continue &&
            opChainContext.inferenceOutputTensorCount != 0 &&
            opChainContext.inferenceOutputTensorCount <= opk::MaxTensorCount) {
            const auto end = opChainContext.inferenceOutputTensors.begin() +
                             opChainContext.inferenceOutputTensorCount;
            const bool validOutputs = std::all_of(opChainContext.inferenceOutputTensors.begin(),
                                                  end,
                                                  [](const auto &view) { return view.isValid(); });
            if (validOutputs) {
                opChainContext.inferencePrompt = std::move(prepared);
            }
        }

        switch (*result) {
        case OpSignal::AbortChain:
            opChainContext.preparedPrompt.reset();
            clearInferenceOutputs(opChainContext);
            return {};
        case OpSignal::BreakLoop:
            opChainContext.preparedPrompt.reset();
            if (inference) {
                clearInferenceOutputs(opChainContext);
            }
            currentIndex =
                (loopId == 0) ? currentIndex + 1 : nextIndexAfterLoop(currentIndex, loopId);
            continue;
        case OpSignal::Continue:
            break;
        }

        const bool lastInChain = currentIndex + 1 == opPtrs.size();
        const size_t nextLoopId = lastInChain ? 0 : opPtrs[currentIndex + 1]->loopId;

        if (loopId != 0 && (lastInChain || nextLoopId != loopId)) {
            currentIndex = firstWorkerIndexForLoop(currentIndex, loopId);
        } else {
            ++currentIndex;
        }
    }

    opChainContext.preparedPrompt.reset();
    return {};
}
