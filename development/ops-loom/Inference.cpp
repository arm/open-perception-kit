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
#include "interface/loom_runtime.h"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <stdexcept>

namespace opk::loom {
struct Inference::State {
    // shortcut: static Float32 host allocations only; extend when another runtime needs more.
    void *library = nullptr;
    lrt_api api{};
    lrt_context context = nullptr;
    lrt_model network = nullptr;
    lrt_session session = nullptr;
    opk::Model model;
    std::vector<lrt_buffer> inputs, outputs;
    std::vector<uint8_t *> inputData, outputData;

    ~State() {
        if (session)
            api.destroy_session(session);
        for (const auto buffer : inputs)
            api.release_buffer(buffer);
        for (const auto buffer : outputs)
            api.release_buffer(buffer);
        if (network)
            api.unload_model(network);
        if (context)
            api.destroy_context(context);
        if (library)
            dlclose(library);
    }
    void check(lrt_status status) const {
        if (status != LRT_OK)
            throw std::runtime_error(
                "LOOM status " + std::to_string(status) + ": " +
                (api.last_error_message ? api.last_error_message() : "bootstrap failed"));
    }
    static opk::Shape shape(const lrt_tensor_info &tensor) {
        opk::Shape result{};
        if (tensor.dtype != LRT_FLOAT32 || tensor.quantization.parameter_count || !tensor.rank ||
            tensor.rank > 8 || !tensor.dimensions)
            throw std::runtime_error(
                "LOOM prototype requires static non-scalar Float32 tensors, rank <= 8");
        result.rank = tensor.rank;
        for (uint32_t i = 0; i < tensor.rank; ++i) {
            if (tensor.dimensions[i] <= 0 || tensor.dimensions[i] > std::numeric_limits<int>::max())
                throw std::runtime_error("LOOM tensor dimension is unsupported");
            result.dims[i] = tensor.dimensions[i];
        }
        return result;
    }
    void allocate(const lrt_tensor_requirement &requirement, lrt_io io, uint32_t index) {
        if (requirement.data_dependent || !requirement.minimum_byte_capacity ||
            !requirement.shape.rank || !requirement.shape.dimensions || !requirement.byte_strides ||
            !requirement.memory_options)
            throw std::runtime_error("LOOM prototype requires concrete dense host tensors");
        const auto expected =
            io == LRT_INPUT ? model.inputs.at(index).shape : model.outputs.at(index).shape;
        if (requirement.shape.rank != expected.rank)
            throw std::runtime_error("LOOM requirement rank differs from model metadata");
        for (uint32_t i = 0; i < requirement.shape.rank; ++i)
            if (requirement.shape.dimensions[i] != static_cast<uint64_t>(expected.dims[i]))
                throw std::runtime_error("LOOM requirement shape differs from model metadata");
        uint64_t stride = sizeof(float);
        for (uint32_t i = requirement.shape.rank; i-- > 0;) {
            if (requirement.byte_strides[i] != stride || !requirement.shape.dimensions[i] ||
                requirement.shape.dimensions[i] > std::numeric_limits<uint64_t>::max() / stride)
                throw std::runtime_error("LOOM tensor is not dense Float32");
            stride *= requirement.shape.dimensions[i];
        }
        if (stride > requirement.minimum_byte_capacity)
            throw std::runtime_error("LOOM tensor capacity is too small");
        const lrt_memory_option *selected = nullptr;
        for (uint32_t i = 0; i < requirement.memory_option_count; ++i) {
            const auto &option = requirement.memory_options[i];
            const auto &c = option.configuration;
            if (c.owner == LRT_OWNER_RUNTIME && c.accessibility == LRT_ACCESSIBLE_HOST &&
                c.reference == LRT_REFERENCE_POINTER && c.transfer == LRT_TRANSFER_DIRECT &&
                (option.pointer_binding_flags & LRT_POINTER_RUNTIME_ALLOCATION) &&
                (io == LRT_INPUT || option.output_handoff == LRT_HANDOFF_BORROWED)) {
                selected = &option;
                break;
            }
        }
        if (!selected)
            throw std::runtime_error("LOOM runtime has no supported host allocation option");
        auto &buffers = io == LRT_INPUT ? inputs : outputs;
        auto &pointers = io == LRT_INPUT ? inputData : outputData;
        lrt_allocation_desc allocation{};
        allocation.configuration = selected->configuration;
        allocation.access = io == LRT_INPUT ? LRT_READ : LRT_WRITE;
        allocation.byte_length =
            std::max(requirement.minimum_byte_capacity, selected->minimum_byte_capacity);
        allocation.byte_alignment = std::max(requirement.byte_alignment, selected->byte_alignment);
        lrt_buffer buffer = nullptr;
        check(api.allocate_buffer(context, &allocation, &buffer));
        buffers.push_back(buffer);
        lrt_buffer_info info{};
        check(api.get_buffer_info(buffer, &info));
        if (!info.host_data || info.view_byte_length < allocation.byte_length)
            throw std::runtime_error("LOOM runtime did not provide requested host storage");
        std::memset(info.host_data, 0, allocation.byte_length);
        pointers.push_back(static_cast<uint8_t *>(info.host_data));
        lrt_tensor_binding binding{};
        binding.buffer = buffer;
        binding.byte_length = allocation.byte_length;
        binding.shape = requirement.shape;
        binding.byte_strides = requirement.byte_strides;
        binding.configuration = selected->configuration;
        check(api.bind_tensor(session, io, index, &binding));
    }
    void setup(const opk::ModelDescriptor &descriptor) {
        if (descriptor.runtimeLibrary.empty() || descriptor.dynamicOutput ||
            !descriptor.tensorFeedbacks.empty())
            throw std::runtime_error(
                "LOOM requires runtimeLibrary, static tensors and no feedback");
        library = dlopen(descriptor.runtimeLibrary.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library)
            throw std::runtime_error(dlerror());
        using GetApi = lrt_status(LRT_CALL *)(uint32_t, lrt_api *);
        const auto getApi = reinterpret_cast<GetApi>(dlsym(library, "lrt_get_api"));
        if (!getApi)
            throw std::runtime_error("LOOM runtime lacks lrt_get_api");
        check(getApi(LRT_RUNTIME_VERSION, &api));
        if (!api.get_runtime_version || api.get_runtime_version() != LRT_RUNTIME_VERSION ||
            !api.create_context || !api.get_capabilities || !api.load_model ||
            !api.describe_model || !api.create_session || !api.get_io_requirements ||
            !api.allocate_buffer || !api.get_buffer_info || !api.bind_tensor ||
            !api.validate_bindings || !api.infer || !api.wait || !api.release_completion ||
            !api.release_buffer || !api.destroy_session || !api.unload_model ||
            !api.destroy_context)
            throw std::runtime_error("LOOM runtime table is incompatible or incomplete");
        check(api.create_context(&context));
        lrt_capabilities capabilities{};
        check(api.get_capabilities(context, &capabilities));
        if (!(capabilities.flags & LRT_CAP_SYNC_INFERENCE))
            throw std::runtime_error("LOOM prototype requires synchronous inference");
        lrt_model_source source{};
        source.kind = LRT_MODEL_SOURCE_PATH;
        source.source.path = descriptor.modelFile.c_str();
        check(api.load_model(context, &source, &network));
        lrt_model_info description{};
        check(api.describe_model(network, &description));
        if (!description.input_count || !description.output_count || description.stateful ||
            description.input_count > opk::MaxTensorCount ||
            description.output_count > opk::MaxTensorCount || !description.inputs ||
            !description.outputs)
            throw std::runtime_error(
                "LOOM prototype requires a stateless model within OPK tensor limits");
        model.engine = "loom";
        for (uint32_t i = 0; i < description.input_count; ++i) {
            if (!description.inputs[i] || !description.inputs[i]->name)
                throw std::runtime_error("LOOM invalid input metadata");
            const auto &tensor = *description.inputs[i];
            model.inputs.push_back({.name = tensor.name,
                                    .valueType = opk::Dtype::Float32,
                                    .shape = shape(tensor),
                                    .quantArguments = {},
                                    .valueInputs = {}});
        }
        for (uint32_t i = 0; i < description.output_count; ++i) {
            if (!description.outputs[i] || !description.outputs[i]->name)
                throw std::runtime_error("LOOM invalid output metadata");
            const auto &tensor = *description.outputs[i];
            model.outputs.push_back({.name = tensor.name,
                                     .valueType = opk::Dtype::Float32,
                                     .shape = shape(tensor),
                                     .quantArguments = {}});
        }
        const auto applied = model.applyModelFromDescriptor(descriptor);
        if (!applied)
            throw std::runtime_error(applied.error().info);
        for (const auto &tensor : descriptor.inputTensors)
            if (tensor.dtype != opk::Dtype::Float32)
                throw std::runtime_error("LOOM input descriptor dtype must be Float32");
        for (const auto &tensor : descriptor.outputTensors)
            if (tensor.dtype != opk::Dtype::Float32)
                throw std::runtime_error("LOOM output descriptor dtype must be Float32");
        check(api.create_session(network, nullptr, &session));
        lrt_io_requirements requirements{};
        check(api.get_io_requirements(session, &requirements));
        if (requirements.input_count != description.input_count ||
            requirements.output_count != description.output_count || !requirements.inputs ||
            !requirements.outputs)
            throw std::runtime_error("LOOM requirement counts do not match model");
        inputs.reserve(requirements.input_count);
        inputData.reserve(requirements.input_count);
        outputs.reserve(requirements.output_count);
        outputData.reserve(requirements.output_count);
        for (uint32_t i = 0; i < requirements.input_count; ++i) {
            if (!requirements.inputs[i])
                throw std::runtime_error("LOOM null input requirement");
            allocate(*requirements.inputs[i], LRT_INPUT, i);
        }
        for (uint32_t i = 0; i < requirements.output_count; ++i) {
            if (!requirements.outputs[i])
                throw std::runtime_error("LOOM null output requirement");
            allocate(*requirements.outputs[i], LRT_OUTPUT, i);
        }
        check(api.validate_bindings(session));
        for (size_t i = 0; i < model.inputs.size(); ++i) {
            const auto &input = model.inputs[i];
            if (opk::isScalarDataKind(input.dataKind)) {
                if (input.valueInputs.size() != input.shape.getFullValueCount())
                    throw std::runtime_error("LOOM scalar values do not match shape");
                std::memcpy(inputData[i],
                            input.valueInputs.data(),
                            input.valueInputs.size() * sizeof(float));
            }
        }
    }
};
Inference::Inference() = default;
Inference::~Inference() = default;
opk::Result<void> Inference::setupFromJson(const std::string &path) {
    state.reset();
    const auto descriptor = opk::ModelDescriptor::fromFile(path);
    if (!descriptor)
        return tl::unexpected(descriptor.error());
    try {
        auto candidate = std::make_unique<State>();
        candidate->setup(*descriptor);
        state = std::move(candidate);
        return {};
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError, error.what()));
    }
}
opk::Result<void> Inference::inference() {
    try {
        lrt_completion completion = nullptr;
        state->check(state->api.infer(state->session, &completion));
        const auto result = state->api.wait(completion, LRT_WAIT_FOREVER);
        const auto released = state->api.release_completion(completion);
        state->check(result);
        state->check(released);
        return {};
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtInferenceError, error.what()));
    }
}
const opk::Model &Inference::getModel() const {
    return state->model;
}
uint8_t *Inference::getInputTensorDataAddress(size_t index) const {
    return state->inputData.at(index);
}
const uint8_t *Inference::getOutputTensorDataAddress(size_t index) const {
    return state->outputData.at(index);
}
opk::Shape Inference::getOutputTensorFinalShape(size_t index) const {
    return state->model.outputs.at(index).shape;
}
} // namespace opk::loom
