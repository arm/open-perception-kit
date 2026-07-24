/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <cassert>
#include <cstddef>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>
#include <memory>
#include <vector>

#include "Log.h"
#include "executorch/runtime/core/error.h"
#include "fmt/base.h"
#include "pek/Model.h"
#include "pek/Result.h"
#include "pek/Types.h"

#include "Inference.h"

static bool to_pek_dtype(executorch::aten::ScalarType t, pek::Dtype &outType) {
    using executorch::aten::ScalarType;
    switch (t) {
    case ScalarType::Byte:
        outType = pek::Dtype::Uint8;
        return true;
    case ScalarType::Char:
        outType = pek::Dtype::Int8;
        return true;
    case ScalarType::Long:
        outType = pek::Dtype::Int64;
        return true;
    case ScalarType::Half:
        outType = pek::Dtype::Float16;
        return true;
    case ScalarType::Float:
        outType = pek::Dtype::Float32;
        return true;
    default:
        return false;
    }
}

// ExecuTorch uses its own scalar type enum, but PEK descriptors use pek::Dtype.
static bool to_executorch_dtype(pek::Dtype t, executorch::aten::ScalarType &outType) {
    using executorch::aten::ScalarType;
    switch (t) {
    case pek::Dtype::Uint8:
        outType = ScalarType::Byte;
        return true;
    case pek::Dtype::Int8:
        outType = ScalarType::Char;
        return true;
    case pek::Dtype::Int64:
        outType = ScalarType::Long;
        return true;
    case pek::Dtype::Float16:
        outType = ScalarType::Half;
        return true;
    case pek::Dtype::Float32:
        outType = ScalarType::Float;
        return true;
    }
    return false;
}

static std::vector<executorch::aten::SizesType> to_executorch_shape(const pek::Shape &shape) {
    std::vector<executorch::aten::SizesType> sizes;
    sizes.reserve(shape.rank);
    for (size_t i = 0; i < shape.rank; ++i) {
        sizes.push_back(static_cast<executorch::aten::SizesType>(shape.dims[i]));
    }
    return sizes;
}

using namespace pek::extrch;

Inference::Inference() = default;
Inference::~Inference() = default;

pek::Result<pek::Model> Inference::inspectModel(executorch::extension::Module &module) {
    pek::Model model;
    model.engine = "executorch";

    // method_names() forces program load on first call.
    const auto names = module.method_names();
    if (!names.ok()) {
        pek::loge("Failed to query method names: error={}\n", static_cast<int>(names.error()));

        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InferenceRtGenericError,
                      fmt::format("Failed to query method names: error={}", (int)names.error()))};
    }

    bool haveForward = false;
    for (const auto &method_name : *names) {
        if (method_name == "forward") {
            haveForward = true;

            // PEK currently treats forward() as the only executable model entry point.
            const auto mm = module.method_meta(method_name);
            if (!mm.ok()) {
                return tl::unexpected{PEK_ERROR(
                    pek::ErrorFlag::InferenceRtGenericError,
                    fmt::format("Failed to get data of forward(): error={}", (int)mm.error()))};
            }

            // Inputs
            for (size_t i = 0; i < (size_t)mm->num_inputs(); ++i) {
                const auto tm = mm->input_tensor_meta(i);
                if (!tm.ok()) {
                    return tl::unexpected{PEK_ERROR(
                        pek::ErrorFlag::InferenceRtGenericError,
                        fmt::format("Failed to get data of input: error={}", (int)tm.error()))};
                }

                pek::ModelInput input;
                input.name = fmt::format("input{}", i);
                if (!to_pek_dtype(tm->scalar_type(), input.valueType)) {
                    return tl::unexpected{
                        PEK_ERROR(pek::ErrorFlag::ModelInspectError,
                                  fmt::format("cannot recognize input ExecuTorch type: {}",
                                              static_cast<int>(tm->scalar_type())))};
                }

                auto sizes = tm->sizes();
                auto shape = detail::toPekShape(sizes);
                if (!shape) {
                    return tl::unexpected{
                        PEK_ERROR(pek::ErrorFlag::ModelInspectError,
                                  fmt::format("input tensor rank must be between 1 and {}",
                                              pek::Shape::MaxRank))};
                }
                input.shape = *shape;
                input.batch = static_cast<int>(sizes[0]);

                model.inputs.push_back(input);
            }

            // Outputs
            for (size_t i = 0; i < (size_t)mm->num_outputs(); ++i) {
                const auto tm = mm->output_tensor_meta(i);
                if (!tm.ok()) {
                    return tl::unexpected{PEK_ERROR(
                        pek::ErrorFlag::InferenceRtGenericError,
                        fmt::format("Failed to get data of output: error={}", (int)tm.error()))};
                }

                pek::ModelOutput output;
                output.name = fmt::format("output{}", i);
                if (!to_pek_dtype(tm->scalar_type(), output.valueType)) {
                    return tl::unexpected{
                        PEK_ERROR(pek::ErrorFlag::ModelInspectError,
                                  fmt::format("cannot recognize output ExecuTorch type: {}",
                                              static_cast<int>(tm->scalar_type())))};
                }

                auto sizes = tm->sizes();
                auto shape = detail::toPekShape(sizes);
                if (!shape) {
                    return tl::unexpected{
                        PEK_ERROR(pek::ErrorFlag::ModelInspectError,
                                  fmt::format("output tensor rank must be between 1 and {}",
                                              pek::Shape::MaxRank))};
                }
                output.shape = *shape;

                model.outputs.push_back(output);
            }
        }
    }

    if (!haveForward) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InferenceRtGenericError,
                                        fmt::format("No forward() in model"))};
    }

    if (model.inputs.size() == 0 || model.outputs.size() == 0) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InferenceRtGenericError,
                                        fmt::format("Model input/output config error"))};
    }

    return model;
}

pek::Result<void> Inference::setup(const pek::ModelDescriptor &modelDesc_) {

    module = std::make_unique<executorch::extension::Module>(modelDesc_.modelFile);

    auto modelResult = inspectModel(*module);
    if (!modelResult) {
        return tl::unexpected{modelResult.error()};
    }
    model = *modelResult;
    model.modelFamily = modelDesc_.modelFamily;

    // --- build up model

    std::string modelLog = model.toString();
    pek::log("========= Original executorch model ========\n");
    pek::log("{}", modelLog);
    pek::log("========= ======== ==== ========== =========\n");

    auto cmResult = model.applyModelFromDescriptor(modelDesc_);
    if (!cmResult) {
        return tl::make_unexpected(cmResult.error());
    }

    // After this point, descriptor shapes/types are the PEK runtime contract.
    if (model.inputs.size() > pek::MaxTensorCount || model.outputs.size() > pek::MaxTensorCount) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InferenceRtModelLoadError,
                                        "model tensor count exceeds max supported"));
    }

    setTensorSizes();

    this->setupReady = true;

    // ---

    modelLog = model.toString();
    pek::log("======= Model updated with json ======\n");
    pek::log("{}", modelLog);
    pek::log("========= ================== =========\n");

    return {};
}

void Inference::setTensorSizes() {

    inputTensors.resize(model.inputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        // Preprocess fills this buffer through getInputTensorDataAddress().
        size_t tensorValueCount = model.inputs[i].shape.getFullValueCount();
        size_t tensorByteCount =
            tensorValueCount * pek::getValueTypeByteSize(model.inputs[i].valueType);
        inputTensors[i].resize(tensorByteCount);
        pek::log("Executorch input tensor prepared: {} bytes\n", tensorByteCount);
    }
}

pek::Result<void> Inference::inference() {
    using executorch::extension::from_blob;
    using executorch::runtime::EValue;

    if (!setupReady || !module) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InferenceRtInferenceError, "ExecuTorch model is not ready"));
    }

    for (size_t i = 0; i < pek::MaxTensorCount; i++) {
        outputTensorPointers[i] = nullptr;
        outputTensorFinalShapes[i] = pek::Shape();
    }

    std::vector<executorch::extension::TensorPtr> inputTensorRefs;
    std::vector<EValue> inputValues;
    inputTensorRefs.reserve(model.inputs.size());
    inputValues.reserve(model.inputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        executorch::aten::ScalarType scalarType;
        if (!to_executorch_dtype(model.inputs[i].valueType, scalarType)) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("unsupported ExecuTorch input dtype at index {}", i))};
        }

        // from_blob does not own data, so keep TensorPtr alive until forward() returns.
        inputTensorRefs.push_back(from_blob(
            inputTensors[i].data(), to_executorch_shape(model.inputs[i].shape), scalarType));
        inputValues.emplace_back(*inputTensorRefs.back());
    }

    auto result = module->forward(inputValues);
    if (!result.ok()) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InferenceRtInferenceError,
            fmt::format("ExecuTorch forward failed: error={}", static_cast<int>(result.error())))};
    }

    // Keep returned EValues alive; TensorViews published below point into them.
    lastOutputs = std::move(*result);
    if (lastOutputs.size() != model.outputs.size()) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ExecuTorch output count {} does not match model output count {}",
                                  lastOutputs.size(),
                                  model.outputs.size()))};
    }

    for (size_t i = 0; i < lastOutputs.size(); i++) {
        if (!lastOutputs[i].isTensor()) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("ExecuTorch output {} is not a tensor", i))};
        }

        const auto &tensor = lastOutputs[i].toTensor();
        pek::Dtype outputType;
        if (!to_pek_dtype(tensor.scalar_type(), outputType)) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("unsupported ExecuTorch output dtype at index {}", i))};
        }

        if (outputType != model.outputs[i].valueType) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("ExecuTorch output dtype mismatch at index {}", i))};
        }

        auto outputShape = detail::toPekShape(tensor.sizes());
        if (!outputShape) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("ExecuTorch output tensor rank at index {} must be between 1 and {}",
                            i,
                            pek::Shape::MaxRank))};
        }

        // Downstream postprocess receives non-owning views over these addresses.
        outputTensorPointers[i] = static_cast<const uint8_t *>(tensor.const_data_ptr());
        outputTensorFinalShapes[i] = *outputShape;
    }

    return {};
}
