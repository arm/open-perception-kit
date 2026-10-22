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
#include "Inference.h"

#include "GraphContext.h"
#include "TensorLayout.h"

#include <mlworkloadlib/session.hpp>

#include <cstring>
#include <stdexcept>

namespace opk::mlwl {
namespace wl = mlsdk::workloadlib;

namespace {

class MappedTensor {
  public:
    MappedTensor(wl::Context &context, wl::ResourceView resource)
        : device(context.contextView().device), allocation(context.createTensor(resource)) {
        const auto memory = allocation.memory();
        const auto bytes = tensorLayout(resource).bytes;
        if (memory.size < bytes)
            throw std::runtime_error("MLWL tensor allocation is smaller than its resource");
        data = static_cast<uint8_t *>(
            (*device.get())
                .mapMemory(
                    memory.memory, memory.offset, memory.size, {}, *device.get().getDispatcher()));
        std::memset(data, 0, bytes);
    }

    ~MappedTensor() {
        (*device.get()).unmapMemory(allocation.memory().memory, *device.get().getDispatcher());
    }

    MappedTensor(const MappedTensor &) = delete;
    MappedTensor &operator=(const MappedTensor &) = delete;

    std::reference_wrapper<const vk::raii::Device> device;
    wl::TensorAllocation allocation;
    uint8_t *data = nullptr;
};

} // namespace

struct Inference::State {
    explicit State(const std::string &path) : workload(wl::Workload::fromVGF(path)) {}

    wl::Workload workload;
    opk::Model model;
    std::unique_ptr<GraphContext> graphContext;
    std::unique_ptr<wl::Context> context;
    std::unique_ptr<wl::Session> session;
    std::vector<std::unique_ptr<MappedTensor>> inputs, outputs;
    std::unique_ptr<wl::BindingSet> bindings;
    std::unique_ptr<wl::PreparedExecution> execution;
};

Inference::Inference() = default;
Inference::~Inference() = default;

opk::Result<void> Inference::setupFromJson(const std::string &filePath) {
    state.reset();
    const auto descriptor = opk::ModelDescriptor::fromFile(filePath);
    if (!descriptor)
        return tl::unexpected(descriptor.error());
    return setup(*descriptor);
}

opk::Result<void> Inference::setup(const opk::ModelDescriptor &descriptor) {
    state.reset();
    try {
        if (descriptor.dynamicOutput || !descriptor.tensorFeedbacks.empty())
            throw std::runtime_error("MLWL requires static tensors without tensor feedback");
        auto candidate = std::make_unique<State>(descriptor.modelFile);
        if (candidate->workload.placeholderModuleCount())
            throw std::runtime_error("MLWL model contains unbound placeholder modules");
        std::vector<wl::ResourceView> inputResources, outputResources;
        candidate->model.engine = "mlwl";
        for (const auto resource : candidate->workload.resources()) {
            const auto layout = tensorLayout(resource);
            if (resource.access() == wl::ResourceAccess::Read) {
                opk::ModelInput input;
                input.name = std::string(resource.name());
                input.valueType = layout.dtype;
                input.shape = layout.shape;
                candidate->model.inputs.push_back(std::move(input));
                inputResources.push_back(resource);
            } else if (resource.access() == wl::ResourceAccess::Write) {
                opk::ModelOutput output;
                output.name = std::string(resource.name());
                output.valueType = layout.dtype;
                output.shape = layout.shape;
                candidate->model.outputs.push_back(std::move(output));
                outputResources.push_back(resource);
            } else {
                throw std::runtime_error(
                    "MLWL cannot expose a read/write resource as an OPK tensor");
            }
        }
        if (inputResources.empty() || outputResources.empty() ||
            inputResources.size() > opk::MaxTensorCount ||
            outputResources.size() > opk::MaxTensorCount)
            throw std::runtime_error("MLWL model input/output count is unsupported");
        if (auto result = candidate->model.applyModelFromDescriptor(descriptor); !result)
            return tl::unexpected(result.error());
        for (size_t i = 0; i < inputResources.size(); ++i) {
            const auto &input = candidate->model.inputs[i];
            if (input.valueType != descriptor.inputTensors[i].dtype)
                throw std::runtime_error("MLWL input type does not match the model descriptor");
            if (opk::isScalarDataKind(input.dataKind) &&
                input.valueInputs.size() != input.shape.getFullValueCount())
                throw std::runtime_error("MLWL scalar/vector values do not match the tensor size");
        }
        for (size_t i = 0; i < outputResources.size(); ++i) {
            if (candidate->model.outputs[i].valueType != descriptor.outputTensors[i].dtype)
                throw std::runtime_error("MLWL output type does not match the model descriptor");
        }

        bool graphOnly = true;
        for (const auto executable : candidate->workload.executables())
            graphOnly &= executable.type() == wl::ExecutableKind::Graph;
        if (graphOnly) {
            candidate->graphContext = std::make_unique<GraphContext>();
            candidate->context.reset(
                new wl::Context(wl::Context::wrap(candidate->graphContext->view())));
        } else {
            candidate->context.reset(new wl::Context(wl::Context::create()));
        }
        candidate->session =
            std::make_unique<wl::Session>(*candidate->context, candidate->workload);
        candidate->session->configure();
        candidate->bindings =
            std::make_unique<wl::BindingSet>(candidate->session->createBindingSet());
        auto allocate = [&](const auto &resources, auto &tensors) {
            for (const auto resource : resources) {
                auto tensor = std::make_unique<MappedTensor>(*candidate->context, resource);
                candidate->bindings->bindTensor(
                    resource, {tensor->allocation.handle(), tensor->allocation.memory()});
                tensors.push_back(std::move(tensor));
            }
        };
        allocate(inputResources, candidate->inputs);
        for (size_t i = 0; i < candidate->inputs.size(); ++i) {
            const auto &input = candidate->model.inputs[i];
            if (!opk::isScalarDataKind(input.dataKind))
                continue;
            // OPK's descriptor schema restricts Value/Vector inputs to Float32.
            std::memcpy(candidate->inputs[i]->data,
                        input.valueInputs.data(),
                        input.valueInputs.size() * sizeof(float));
        }
        allocate(outputResources, candidate->outputs);
        candidate->execution = std::make_unique<wl::PreparedExecution>(
            candidate->session->prepare(*candidate->bindings));
        state = std::move(candidate);
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        "MLWL startup: " + std::string(error.what())));
    }
    return {};
}

bool Inference::isReady() const {
    return state != nullptr;
}

const opk::Model &Inference::getModel() const {
    if (!state)
        throw std::logic_error("MLWL inference has not been configured");
    return state->model;
}

uint8_t *Inference::getInputTensorDataAddress(size_t index) {
    if (!state)
        throw std::logic_error("MLWL inference has not been configured");
    return state->inputs.at(index)->data;
}

const uint8_t *Inference::getOutputTensorDataAddress(size_t index) const {
    if (!state)
        throw std::logic_error("MLWL inference has not been configured");
    return state->outputs.at(index)->data;
}

opk::Shape Inference::getOutputTensorFinalShape(size_t index) const {
    return getModel().outputs.at(index).shape;
}

opk::Result<void> Inference::inference() {
    if (!state)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        "MLWL inference has not been configured"));
    try {
        // MLWL waits for completion; coherent mapped tensors remain valid until teardown.
        state->execution->run();
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtInferenceError,
                                        "MLWL execution: " + std::string(error.what())));
    }
    return {};
}

} // namespace opk::mlwl
