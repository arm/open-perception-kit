/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <algorithm>
#include <cstddef>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>
#include <memory>
#include <vector>

#include "Log.h"
#include "executorch/runtime/core/error.h"
#include "fmt/base.h"
#include "opk/Model.h"
#include "opk/Result.h"
#include "opk/Types.h"

#include "Inference.h"

static bool to_opk_dtype(executorch::aten::ScalarType t, opk::Dtype &outType) {
    using executorch::aten::ScalarType;
    switch (t) {
    case ScalarType::Byte:
        outType = opk::Dtype::Uint8;
        return true;
    case ScalarType::Char:
        outType = opk::Dtype::Int8;
        return true;
    case ScalarType::Long:
        outType = opk::Dtype::Int64;
        return true;
    case ScalarType::Half:
        outType = opk::Dtype::Float16;
        return true;
    case ScalarType::Float:
        outType = opk::Dtype::Float32;
        return true;
    default:
        return false;
    }
}

// ExecuTorch uses its own scalar type enum, but OPK descriptors use opk::Dtype.
static bool to_executorch_dtype(opk::Dtype t, executorch::aten::ScalarType &outType) {
    using executorch::aten::ScalarType;
    switch (t) {
    case opk::Dtype::Uint8:
        outType = ScalarType::Byte;
        return true;
    case opk::Dtype::Int8:
        outType = ScalarType::Char;
        return true;
    case opk::Dtype::Int64:
        outType = ScalarType::Long;
        return true;
    case opk::Dtype::Float16:
        outType = ScalarType::Half;
        return true;
    case opk::Dtype::Float32:
        outType = ScalarType::Float;
        return true;
    }
    return false;
}

template <typename SizesT> static opk::Shape to_opk_shape(const SizesT &sizes) {
    opk::Shape s{};

    // OPK Shape has fixed storage for 8 dimensions.
    const size_t maxDims = sizeof(s.dims) / sizeof(s.dims[0]);
    if (sizes.size() > maxDims)
        return s;

    s.rank = sizes.size();
    for (size_t i = 0; i < sizes.size(); ++i) {
        s.dims[i] = static_cast<int>(sizes[i]);
    }

    for (size_t i = sizes.size(); i < maxDims; ++i) {
        s.dims[i] = 0;
    }

    return s;
}

static std::vector<executorch::aten::SizesType> to_executorch_shape(const opk::Shape &shape) {
    std::vector<executorch::aten::SizesType> sizes;
    sizes.reserve(shape.rank);
    for (size_t i = 0; i < shape.rank; ++i) {
        sizes.push_back(static_cast<executorch::aten::SizesType>(shape.dims[i]));
    }
    return sizes;
}

using namespace opk::extrch;

Inference::Inference() = default;
Inference::~Inference() = default;

opk::Result<void> Inference::setupFromJson(const std::string &filePath) {
    auto descResult = opk::ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }
    return setup(*descResult);
}

opk::Result<opk::Model> Inference::inspectModel(executorch::extension::Module &module) {
    opk::Model model;
    model.engine = "executorch";

    // method_names() forces program load on first call.
    const auto names = module.method_names();
    if (!names.ok()) {
        opk::log::error("Failed to query method names: error={}\n",
                        static_cast<int>(names.error()));

        return tl::unexpected{
            OPK_ERROR(opk::ErrorFlag::InferenceRtGenericError,
                      fmt::format("Failed to query method names: error={}", (int)names.error()))};
    }

    bool haveForward = false;
    for (const auto &method_name : *names) {
        if (method_name == "forward") {
            haveForward = true;

            // OPK currently treats forward() as the only executable model entry point.
            const auto mm = module.method_meta(method_name);
            if (!mm.ok()) {
                return tl::unexpected{OPK_ERROR(
                    opk::ErrorFlag::InferenceRtGenericError,
                    fmt::format("Failed to get data of forward(): error={}", (int)mm.error()))};
            }

            // Inputs
            for (size_t i = 0; i < (size_t)mm->num_inputs(); ++i) {
                const auto tm = mm->input_tensor_meta(i);
                if (!tm.ok()) {
                    return tl::unexpected{OPK_ERROR(
                        opk::ErrorFlag::InferenceRtGenericError,
                        fmt::format("Failed to get data of input: error={}", (int)tm.error()))};
                }

                opk::ModelInput input;
                input.name = fmt::format("input{}", i);
                if (!to_opk_dtype(tm->scalar_type(), input.valueType)) {
                    return tl::unexpected{
                        OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                  fmt::format("cannot recognize input ExecuTorch type: {}",
                                              static_cast<int>(tm->scalar_type())))};
                }

                auto sizes = tm->sizes();
                if (sizes.size() < 1 || sizes.size() > std::size(input.shape.dims)) {
                    opk::log::error("ExecuTorch input tensor {} size {} must be between 1 and 8\n",
                                    i,
                                    sizes.size());
                    return tl::unexpected{OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                                    "input tensor size must be between 1 and 8")};
                }
                input.shape = to_opk_shape(sizes);
                input.batch = (sizes.size() > 0) ? static_cast<int>(sizes[0]) : 0;

                model.inputs.push_back(input);
            }

            // Outputs
            for (size_t i = 0; i < (size_t)mm->num_outputs(); ++i) {
                const auto tm = mm->output_tensor_meta(i);
                if (!tm.ok()) {
                    return tl::unexpected{OPK_ERROR(
                        opk::ErrorFlag::InferenceRtGenericError,
                        fmt::format("Failed to get data of output: error={}", (int)tm.error()))};
                }

                opk::ModelOutput output;
                output.name = fmt::format("output{}", i);
                if (!to_opk_dtype(tm->scalar_type(), output.valueType)) {
                    return tl::unexpected{
                        OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                  fmt::format("cannot recognize output ExecuTorch type: {}",
                                              static_cast<int>(tm->scalar_type())))};
                }

                auto sizes = tm->sizes();
                if (sizes.size() < 1 || sizes.size() > std::size(output.shape.dims)) {
                    opk::log::error("ExecuTorch output tensor {} size {} must be between 1 and 8\n",
                                    i,
                                    sizes.size());
                    return tl::unexpected{OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                                    "output tensor size must be between 1 and 8")};
                }
                output.shape = to_opk_shape(sizes);

                model.outputs.push_back(output);
            }
        }
    }

    if (!haveForward) {
        return tl::unexpected{OPK_ERROR(opk::ErrorFlag::InferenceRtGenericError,
                                        fmt::format("No forward() in model"))};
    }

    if (model.inputs.size() == 0 || model.outputs.size() == 0) {
        return tl::unexpected{OPK_ERROR(opk::ErrorFlag::InferenceRtGenericError,
                                        fmt::format("Model input/output config error"))};
    }

    return model;
}

opk::Result<void> Inference::setup(const opk::ModelDescriptor &modelDesc_) {

    modelDescriptor = modelDesc_;
    modelPath = modelDesc_.modelFile;

    module = std::make_unique<executorch::extension::Module>(modelPath);

    auto modelResult = inspectModel(*module);
    if (!modelResult) {
        return tl::unexpected{modelResult.error()};
    }
    model = *modelResult;

    // --- build up model

    std::string modelLog = model.toString();
    opk::log::info("========= Original executorch model ========\n");
    opk::log::info("{}", modelLog);
    opk::log::info("========= ======== ==== ========== =========\n");

    auto cmResult = model.applyModelFromDescriptor(modelDescriptor);
    if (!cmResult) {
        return tl::unexpected(cmResult.error());
    }

    // After this point, descriptor shapes/types are the OPK runtime contract.
    if (model.inputs.size() > opk::MaxTensorCount || model.outputs.size() > opk::MaxTensorCount) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtModelLoadError,
                                        "model tensor count exceeds max supported"));
    }

    setTensorSizes();

    this->setupReady = true;

    // ---

    modelLog = model.toString();
    opk::log::info("======= Model updated with json ======\n");
    opk::log::info("{}", modelLog);
    opk::log::info("========= ================== =========\n");

    return {};
}

void Inference::setTensorSizes() {

    inputTensors.resize(model.inputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        // Preprocess fills this buffer through getInputTensorDataAddress().
        size_t tensorValueCount = model.inputs[i].shape.getFullValueCount();
        size_t tensorByteCount =
            tensorValueCount * opk::getValueTypeByteSize(model.inputs[i].valueType);
        inputTensors[i].resize(tensorByteCount);
        opk::log::info("Executorch input tensor prepared: {} bytes\n", tensorByteCount);
    }
}

opk::Result<void> Inference::inference() {
    using executorch::extension::from_blob;
    using executorch::runtime::EValue;

    if (!setupReady || !module) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InferenceRtInferenceError, "ExecuTorch model is not ready"));
    }

    for (size_t i = 0; i < opk::MaxTensorCount; i++) {
        outputTensorPointers[i] = nullptr;
        outputTensorFinalShapes[i] = opk::Shape();
    }

    std::vector<executorch::extension::TensorPtr> inputTensorRefs;
    std::vector<EValue> inputValues;
    inputTensorRefs.reserve(model.inputs.size());
    inputValues.reserve(model.inputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        executorch::aten::ScalarType scalarType;
        if (!to_executorch_dtype(model.inputs[i].valueType, scalarType)) {
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("unsupported ExecuTorch input dtype at index {}", i))};
        }

        // from_blob does not own data, so keep TensorPtr alive until forward() returns.
        inputTensorRefs.push_back(from_blob(
            inputTensors[i].data(), to_executorch_shape(model.inputs[i].shape), scalarType));
        inputValues.emplace_back(*inputTensorRefs.back());
    }

    auto result = module->forward(inputValues);
    if (!result.ok()) {
        return tl::unexpected{OPK_ERROR(
            opk::ErrorFlag::InferenceRtInferenceError,
            fmt::format("ExecuTorch forward failed: error={}", static_cast<int>(result.error())))};
    }

    // Keep returned EValues alive; TensorViews published below point into them.
    lastOutputs = std::move(*result);
    if (lastOutputs.size() != model.outputs.size()) {
        return tl::unexpected{
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      fmt::format("ExecuTorch output count {} does not match model output count {}",
                                  lastOutputs.size(),
                                  model.outputs.size()))};
    }

    for (size_t i = 0; i < lastOutputs.size(); i++) {
        if (!lastOutputs[i].isTensor()) {
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("ExecuTorch output {} is not a tensor", i))};
        }

        const auto &tensor = lastOutputs[i].toTensor();
        opk::Dtype outputType;
        if (!to_opk_dtype(tensor.scalar_type(), outputType)) {
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("unsupported ExecuTorch output dtype at index {}", i))};
        }

        if (outputType != model.outputs[i].valueType) {
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("ExecuTorch output dtype mismatch at index {}", i))};
        }

        // Downstream postprocess receives non-owning views over these addresses.
        outputTensorPointers[i] = static_cast<const uint8_t *>(tensor.const_data_ptr());
        if (!outputTensorPointers[i]) {
            opk::log::error("ExecuTorch output tensor {} has no data\n", i);
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "ExecuTorch output tensor " + std::to_string(i) + " has no data")};
        }
        if (tensor.sizes().size() < 1 || tensor.sizes().size() > 8) {
            opk::log::error("ExecuTorch output tensor {} size must be between 1 and 8\n", i);
            return tl::unexpected{OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "ExecuTorch output tensor " + std::to_string(i) +
                                                " size must be between 1 and 8")};
        }
        outputTensorFinalShapes[i] = to_opk_shape(tensor.sizes());
    }

    return {};
}
