/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Inference.h"

#define EXECUTORCH_ENABLE_LOGGING 1

#include <cstddef>
#include <cstdio>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>
#include <memory>

#include "executorch/runtime/core/error.h"
#include "fmt/base.h"
#include "pek/Model.h"
#include "pek/Result.h"
#include "pek/String.h"
#include "pek/Types.h"

using ::executorch::aten::ScalarType;
using ::executorch::extension::MethodMeta;
using ::executorch::extension::Module;
using ::executorch::runtime::Result;

// ---

static pek::Dtype to_pek_dtype(executorch::aten::ScalarType t) {
    using executorch::aten::ScalarType;
    switch (t) {
    case ScalarType::Byte:
        return pek::Dtype::Uint8;
    case ScalarType::Char:
        return pek::Dtype::Int8;
    case ScalarType::Long:
        return pek::Dtype::Int64;
    case ScalarType::Half:
        return pek::Dtype::Float16;
    case ScalarType::Float:
        return pek::Dtype::Float32;
    default:
        assert(0);
    }
    return pek::Dtype::Float32;
}

template <typename SizesT> static pek::Shape to_pek_shape(const SizesT &sizes) {
    pek::Shape s{};

    s.rank = static_cast<int>(sizes.size());

    // IMPORTANT: make sure we don't overflow dims
    const size_t maxDims = sizeof(s.dims) / sizeof(s.dims[0]);
    const size_t n = std::min(sizes.size(), maxDims);

    assert(sizes.size() <= maxDims && "Tensor rank exceeds maximum supported Shape rank");

    for (size_t i = 0; i < n; ++i) {
        s.dims[i] = static_cast<int>(sizes[i]);
    }

    // Optional: zero remaining dims for safety
    for (size_t i = n; i < maxDims; ++i) {
        s.dims[i] = 0;
    }

    return s;
}

using namespace pek::extrch;

Inference::Inference() {}
Inference::~Inference() {}

pek::Result<void> Inference::setupFromJson(const std::string &filePath) {

    auto descResult = pek::ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }

    { // setup model file name
        std::string modelRoot = filePath;
        if (pek::utf8::contains(modelRoot, '/')) {
            size_t lastSlashAt = pek::utf8::lastIndexOf(modelRoot, '/');
            modelRoot = pek::utf8::left(modelRoot, lastSlashAt + 1);
        } else {
            modelRoot = "";
        }
        (*descResult).modelFile = modelRoot + (*descResult).modelFile;
    }

    auto setupResult = setup(*descResult);
    if (!setupResult) {
        return tl::unexpected{setupResult.error()};
    }

    return {};
}

pek::Result<pek::Model> Inference::inspectModel(executorch::extension::Module &module) {
    pek::Model model;
    model.engine = "executorch";

    // method_names() forces program load on first call
    const auto names = module.method_names();
    if (!names.ok()) {
        std::printf("Failed to query method names: error=%d\n", (int)names.error());

        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::ExecuTorchError,
                      fmt::format("Failed to query method names: error={}", (int)names.error()))};
    }

    bool haveForward = false;
    for (const auto &method_name : *names) {
        if (method_name == "forward") {
            haveForward = true;

            const auto mm = module.method_meta(method_name);
            if (!mm.ok()) {
                return tl::unexpected{PEK_ERROR(
                    pek::ErrorFlag::ExecuTorchError,
                    fmt::format("Failed to get data of forward(): error={}", (int)mm.error()))};
            }

            // Inputs
            for (size_t i = 0; i < (size_t)mm->num_inputs(); ++i) {
                const auto tm = mm->input_tensor_meta(i);
                if (!tm.ok()) {
                    return tl::unexpected{PEK_ERROR(
                        pek::ErrorFlag::ExecuTorchError,
                        fmt::format("Failed to get data of input: error={}", (int)tm.error()))};
                }

                pek::ModelInput input;
                input.name = fmt::format("input{}", i);
                input.valueType = to_pek_dtype(tm->scalar_type());

                auto sizes = tm->sizes();
                input.shape = to_pek_shape(sizes);
                input.batch = (sizes.size() > 0) ? static_cast<int>(sizes[0]) : 0;

                model.inputs.push_back(input);
            }

            // Outputs
            for (size_t i = 0; i < (size_t)mm->num_outputs(); ++i) {
                const auto tm = mm->output_tensor_meta(i);
                if (!tm.ok()) {
                    return tl::unexpected{PEK_ERROR(
                        pek::ErrorFlag::ExecuTorchError,
                        fmt::format("Failed to get data of output: error={}", (int)tm.error()))};
                }

                pek::ModelOutput output;
                output.name = fmt::format("output{}", i);
                output.valueType = to_pek_dtype(tm->scalar_type());

                auto sizes = tm->sizes();
                output.shape = to_pek_shape(sizes);

                model.outputs.push_back(output);
            }
        }
    }

    if (!haveForward) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::ExecuTorchError, fmt::format("No forward() in model"))};
    }

    if (model.inputs.size() == 0 || model.outputs.size() == 0) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::ExecuTorchError,
                                        fmt::format("Model input/output config error"))};
    }

    return model;
}

pek::Result<void> Inference::setup(const pek::ModelDescriptor &modelDesc_) {

    modelDescriptor = modelDesc_;
    modelPath = modelDesc_.modelFile;

    module = std::make_unique<executorch::extension::Module>(modelPath);

    auto modelResult = inspectModel(*module);
    if (!modelResult) {
        return tl::unexpected{modelResult.error()};
    }
    model = *modelResult;
    model.modelFamily = modelDesc_.modelFamily;

    // --- build up model

    std::string modelLog = model.toString();
    printf("========= Original executorch model ========\n");
    printf("%s", modelLog.c_str());
    printf("========= ======== ==== ========== =========\n");

    auto cmResult = model.applyModelFromDescriptor(modelDescriptor);
    if (!cmResult) {
        return tl::make_unexpected(cmResult.error());
    }

    setTensorSizes();

    this->setupReady = true;

    // ---

    modelLog = model.toString();
    printf("======= Model updated with json ======\n");
    printf("%s", modelLog.c_str());
    printf("========= ================== =========\n");

    Forward();

    return {};
}

void Inference::setTensorSizes() {

    inputTensors.resize(model.inputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        size_t tensorValueCount = model.inputs[i].shape.getFullValueCount();
        size_t tensorByteCount =
            tensorValueCount * pek::getValueTypeByteSize(model.inputs[i].valueType);
        inputTensors[i].resize(tensorByteCount);
        fmt::print("Executorch input tensor prepared: {} bytes\n", tensorByteCount);
    }

    outputTensors.resize(model.outputs.size());

    for (size_t i = 0; i < model.outputs.size(); i++) {
        size_t tensorValueCount = model.outputs[i].shape.getFullValueCount();
        size_t tensorByteCount =
            tensorValueCount * pek::getValueTypeByteSize(model.inputs[i].valueType);
        outputTensors[i].resize(tensorByteCount);
        fmt::print("Executorch output tensor prepared: {} bytes\n", tensorByteCount);
    }
}

void Inference::Forward() {
    using executorch::extension::from_blob;
    using executorch::extension::module::Module;

    // Example input buffer (must match model dtype/shape)

    // Create input tensor view over existing memory
    auto x = from_blob(inputTensors[0].data(), {1, 3, 416, 416});

    auto result = module->forward(x);
    if (!result.ok()) {
        auto err = result.error();
        printf("Executorch forward failed: error code = %d\n", static_cast<int>(err));
        return;
    }

    // Get first output as a Tensor, then get typed pointer

    auto outTensor = result->at(0).toTensor();
    const float *out = outTensor.const_data_ptr<float>();

    std::printf("out[0]=%f\n", out[0]);
}
