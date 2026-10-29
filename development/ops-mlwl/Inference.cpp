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

#include "TensorLayout.h"
#include "vulkan/Input.h"

#include <mlworkloadlib/session.hpp>

#include <algorithm>
#include <cstring>
#include <ranges>
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
        if (memory.size < bytes) {
            throw std::runtime_error("MLWL tensor allocation is smaller than its resource");
        }
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

    void shareBuffer(const size_t bytes) {
        buffer = vk::raii::Buffer(
            device.get(), vk::BufferCreateInfo({}, bytes, vk::BufferUsageFlagBits::eStorageBuffer));
        const auto requirements =
            device.get()
                .getBufferMemoryRequirements2<vk::MemoryRequirements2,
                                              vk::MemoryDedicatedRequirements>(
                    vk::BufferMemoryRequirementsInfo2(*buffer));
        const auto &bufferMemory = requirements.get<vk::MemoryRequirements2>().memoryRequirements;
        const auto tensorMemory = device.get()
                                      .getTensorMemoryRequirementsARM(
                                          vk::TensorMemoryRequirementsInfoARM(allocation.handle()))
                                      .memoryRequirements;
        const auto memory = allocation.memory();
        // MLWL does not expose its chosen memory type: every candidate must support this buffer.
        if (requirements.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation ||
            (tensorMemory.memoryTypeBits & ~bufferMemory.memoryTypeBits) != 0 ||
            memory.offset % bufferMemory.alignment != 0 || memory.size < bufferMemory.size) {
            throw std::runtime_error("MLWL input memory cannot be shared with a Vulkan buffer");
        }
        buffer.bindMemory(memory.memory, memory.offset);
    }

    std::reference_wrapper<const vk::raii::Device> device;
    wl::TensorAllocation allocation;
    vk::raii::Buffer buffer{nullptr};
    uint8_t *data = nullptr;
};

} // namespace

struct Inference::State {
    State(const std::string &path, const bool vulkanInput)
        : workload(wl::Workload::fromVGF(path)), vulkanInput(vulkanInput) {}

    opk::Result<void> configureModel(const opk::ModelDescriptor &descriptor);
    void configureSession();
    wl::BindingSet bindTensors();

    wl::Workload workload;
    const bool vulkanInput;
    opk::Model model;
    std::unique_ptr<wl::Context> context;
    std::unique_ptr<wl::Session> session;
    std::vector<std::unique_ptr<MappedTensor>> inputs, outputs;
    std::unique_ptr<wl::PreparedExecution> execution;
};

opk::Result<void> Inference::State::configureModel(const opk::ModelDescriptor &descriptor) {
    opk::Result<void> result{};
    if (descriptor.dynamicOutput || !descriptor.tensorFeedbacks.empty()) {
        result = tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                          "MLWL requires static tensors without tensor feedback"));
    }

    model.engine = "mlwl";
    for (const auto resource : workload.resources()) {
        if (!result) {
            break;
        }
        const auto layout = tensorLayout(resource);
        switch (resource.access()) {
        case wl::ResourceAccess::Read:
            model.inputs.push_back({.name = std::string(resource.name()),
                                    .valueType = layout.dtype,
                                    .shape = layout.shape,
                                    .quantArguments = {},
                                    .valueInputs = {}});
            break;
        case wl::ResourceAccess::Write:
            model.outputs.push_back({.name = std::string(resource.name()),
                                     .valueType = layout.dtype,
                                     .shape = layout.shape,
                                     .quantArguments = {}});
            break;
        default:
            result = tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                          "MLWL cannot expose a read/write resource as an OPK tensor"));
        }
    }
    if (result &&
        (model.inputs.empty() || model.outputs.empty() ||
         model.inputs.size() > opk::MaxTensorCount || model.outputs.size() > opk::MaxTensorCount)) {
        result = tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                          "MLWL model input/output count is unsupported"));
    }

    if (result) {
        result = model.applyModelFromDescriptor(descriptor);
    }
    for (size_t i = 0; result && i < model.inputs.size(); ++i) {
        const auto &input = model.inputs[i];
        if (input.valueType != descriptor.inputTensors[i].dtype) {
            result =
                tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                         "MLWL input type does not match the model descriptor"));
        } else if (opk::isScalarDataKind(input.dataKind) &&
                   input.valueInputs.size() != input.shape.getFullValueCount()) {
            result =
                tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                         "MLWL scalar/vector values do not match the tensor size"));
        }
    }
    for (size_t i = 0; result && i < model.outputs.size(); ++i) {
        if (model.outputs[i].valueType != descriptor.outputTensors[i].dtype) {
            result =
                tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                         "MLWL output type does not match the model descriptor"));
        }
    }
    return result;
}

void Inference::State::configureSession() {
    const auto executableIndices = std::views::iota(0u, workload.executableCount());
    const bool graphOnly = std::ranges::all_of(executableIndices, [this](const uint32_t index) {
        return workload.executable(index).type() == wl::ExecutableKind::Graph;
    });
    wl::RuntimeContextDeviceRequirements requirements;
    if (graphOnly && !vulkanInput) {
        requirements.capabilities = wl::RuntimeDeviceCapability::DataGraphShaderModules |
                                    wl::RuntimeDeviceCapability::Tensors;
    }
    context.reset(new wl::Context(wl::Context::create(requirements)));
    session = std::make_unique<wl::Session>(*context, workload);
    session->configure();
}

wl::BindingSet Inference::State::bindTensors() {
    auto bindings = session->createBindingSet();
    for (const auto resource : workload.resources()) {
        auto tensor = std::make_unique<MappedTensor>(*context, resource);
        if (vulkanInput && resource.access() == wl::ResourceAccess::Read) {
            tensor->shareBuffer(tensorLayout(resource).bytes);
        }
        bindings.bindTensor(resource, {tensor->allocation.handle(), tensor->allocation.memory()});
        auto &tensors = resource.access() == wl::ResourceAccess::Read ? inputs : outputs;
        tensors.push_back(std::move(tensor));
    }
    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto &input = model.inputs[i];
        if (opk::isScalarDataKind(input.dataKind)) {
            // OPK's descriptor schema restricts Value/Vector inputs to Float32.
            std::memcpy(inputs[i]->data,
                        input.valueInputs.data(),
                        input.valueInputs.size() * sizeof(float));
        }
    }
    return bindings;
}

Inference::Inference(const bool vulkanInput) : vulkanInput(vulkanInput) {}
Inference::~Inference() = default;

opk::Result<void> Inference::setupFromJson(const std::string &filePath) {
    state.reset();
    const auto descriptor = opk::ModelDescriptor::fromFile(filePath);
    if (!descriptor) {
        return tl::unexpected(descriptor.error());
    }
    return setup(*descriptor);
}

opk::Result<void> Inference::setup(const opk::ModelDescriptor &descriptor) {
    state.reset();
    try {
        auto candidate = std::make_shared<State>(descriptor.modelFile, vulkanInput);
        const auto result = candidate->configureModel(descriptor);
        if (result) {
            candidate->configureSession();
            const auto bindings = candidate->bindTensors();
            candidate->execution =
                std::make_unique<wl::PreparedExecution>(candidate->session->prepare(bindings));
            state = std::move(candidate);
        }
        return result;
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        "MLWL startup: " + std::string(error.what())));
    }
}

bool Inference::isReady() const {
    return state != nullptr;
}

const opk::Model &Inference::getModel() const {
    if (!state) {
        throw std::logic_error("MLWL inference has not been configured");
    }
    return state->model;
}

uint8_t *Inference::getInputTensorDataAddress(size_t index) {
    if (!state) {
        throw std::logic_error("MLWL inference has not been configured");
    }
    return state->inputs.at(index)->data;
}

std::shared_ptr<const opk::vulkan::Input> Inference::getVulkanInput(const size_t index) const {
    if (!state || !vulkanInput) {
        throw std::logic_error("MLWL Vulkan input has not been configured");
    }
    const auto context = state->context->contextView();
    return std::make_shared<opk::vulkan::Input>(opk::vulkan::Input{
        .owner = state,
        .physicalDevice = *context.physicalDevice.get(),
        .device = *context.device.get(),
        .queue = *context.queue.get(),
        .queueFamily = context.queueFamilyIndex,
        .buffer = *state->inputs.at(index)->buffer,
        .bytes = state->model.inputs.at(index).shape.getFullValueCount() *
                 opk::getValueTypeByteSize(state->model.inputs.at(index).valueType)});
}

const uint8_t *Inference::getOutputTensorDataAddress(size_t index) const {
    if (!state) {
        throw std::logic_error("MLWL inference has not been configured");
    }
    return state->outputs.at(index)->data;
}

opk::Shape Inference::getOutputTensorFinalShape(size_t index) const {
    return getModel().outputs.at(index).shape;
}

opk::Result<void> Inference::inference() {
    if (!state) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        "MLWL inference has not been configured"));
    }
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
