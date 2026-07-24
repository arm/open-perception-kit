/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Inference.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <thread>
#include <vector>

#include "Log.h"
#include "pek/Result.h"
#include "pek/String.h"
#include "pek/Types.h"
#include "tools.h"

using namespace pek::ncnnrt;

namespace {

size_t scalarValueCount(pek::DataKind kind) {
    switch (kind) {
    case pek::DataKind::Value:
        return 1;
    case pek::DataKind::Vector2:
        return 2;
    case pek::DataKind::Vector3:
        return 3;
    case pek::DataKind::Vector4:
        return 4;
    default:
        return 0;
    }
}

int requirePositiveDim(const pek::Shape &shape, size_t index) {
    assert(index < shape.rank);
    assert(shape.dims[index] > 0);
    return shape.dims[index];
}

size_t compactElementCount(const ncnn::Mat &mat) {
    switch (mat.dims) {
    case 1:
        return static_cast<size_t>(mat.w);
    case 2:
        return static_cast<size_t>(mat.w) * static_cast<size_t>(mat.h);
    case 3:
        return static_cast<size_t>(mat.w) * static_cast<size_t>(mat.h) * static_cast<size_t>(mat.c);
    case 4:
        return static_cast<size_t>(mat.w) * static_cast<size_t>(mat.h) *
               static_cast<size_t>(mat.d) * static_cast<size_t>(mat.c);
    default:
        return 0;
    }
}

pek::Shape shapeFromMat(const ncnn::Mat &mat) {
    pek::Shape shape;

    switch (mat.dims) {
    case 1:
        shape = pek::Shape(mat.w);
        break;
    case 2:
        shape = pek::Shape(mat.h, mat.w);
        break;
    case 3:
        shape = pek::Shape(mat.c, mat.h, mat.w);
        break;
    case 4:
        shape = pek::Shape(mat.c, mat.d, mat.h, mat.w);
        break;
    default:
        break;
    }

    return shape;
}

void copyDenseChwToMat(const float *src, ncnn::Mat &dst) {
    const size_t planeValueCount =
        static_cast<size_t>(dst.w) * static_cast<size_t>(dst.h) * static_cast<size_t>(dst.d);

    for (int c = 0; c < dst.c; c++) {
        ncnn::Mat channel = dst.channel(c);
        std::memcpy(channel.data,
                    src + static_cast<size_t>(c) * planeValueCount,
                    planeValueCount * sizeof(float));
    }
}

void copyDenseHwcToMat(const float *src, ncnn::Mat &dst) {
    const int width = dst.w;
    const int height = dst.h;
    const int channels = dst.c;

    for (int c = 0; c < channels; c++) {
        ncnn::Mat channel = dst.channel(c);
        float *dstData = reinterpret_cast<float *>(channel.data);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                dstData[static_cast<size_t>(y) * width + x] =
                    src[(static_cast<size_t>(y) * width + x) * channels + c];
            }
        }
    }
}

pek::Result<void> copyMatToDenseBuffer(const ncnn::Mat &mat, std::vector<uint8_t> &buffer) {
    if (mat.elempack != 1) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        "NCNN output tensor is still packed after extract")};
    }

    if (mat.elemsize != sizeof(float)) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("NCNN output elemsize {} is not Float32", mat.elemsize))};
    }

    const size_t byteCount = compactElementCount(mat) * sizeof(float);
    buffer.resize(byteCount);

    if (mat.dims == 1 || mat.dims == 2) {
        std::memcpy(buffer.data(), mat.data, byteCount);
        return {};
    }

    if (mat.dims == 3) {
        const size_t planeByteCount =
            static_cast<size_t>(mat.w) * static_cast<size_t>(mat.h) * sizeof(float);
        uint8_t *dst = buffer.data();
        for (int c = 0; c < mat.c; c++) {
            ncnn::Mat channel = mat.channel(c);
            std::memcpy(
                dst + static_cast<size_t>(c) * planeByteCount, channel.data, planeByteCount);
        }
        return {};
    }

    if (mat.dims == 4) {
        const size_t planeByteCount = static_cast<size_t>(mat.w) * static_cast<size_t>(mat.h) *
                                      static_cast<size_t>(mat.d) * sizeof(float);
        uint8_t *dst = buffer.data();
        for (int c = 0; c < mat.c; c++) {
            ncnn::Mat channel = mat.channel(c);
            std::memcpy(
                dst + static_cast<size_t>(c) * planeByteCount, channel.data, planeByteCount);
        }
        return {};
    }

    return tl::unexpected{
        PEK_ERROR(pek::ErrorFlag::InvalidData,
                  fmt::format("unsupported NCNN output tensor rank {}", mat.dims))};
}

} // namespace

Inference::Inference() = default;
Inference::~Inference() = default;

std::string Inference::deriveBinPath(const std::string &paramPath) {
    const std::string suffix = ".param";
    if (paramPath.size() >= suffix.size() &&
        paramPath.compare(paramPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return paramPath.substr(0, paramPath.size() - suffix.size()) + ".bin";
    }

    return paramPath + ".bin";
}

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

pek::Result<pek::Model> Inference::buildModelFromDescriptor(const pek::ModelDescriptor &desc,
                                                            const ncnn::Net &net) {
    if (desc.dynamicOutput) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        "NCNN backend requires static output descriptors")};
    }

    if (desc.inputTensors.empty() || desc.outputTensors.empty()) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      "NCNN model descriptor must define inputTensors and outputTensors")};
    }

    const std::vector<const char *> &inputNames = net.input_names();
    const std::vector<const char *> &outputNames = net.output_names();

    if (desc.inputTensors.size() != inputNames.size()) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("NCNN input tensor count mismatch: descriptor has {}, model "
                                  "has {}",
                                  desc.inputTensors.size(),
                                  inputNames.size()))};
    }

    if (desc.outputTensors.size() != outputNames.size()) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("NCNN output tensor count mismatch: descriptor has {}, model "
                                  "has {}",
                                  desc.outputTensors.size(),
                                  outputNames.size()))};
    }

    pek::Model model;
    model.engine = "ncnn";
    model.modelFamily = desc.modelFamily;
    model.contentType = desc.contentType;
    model.inputs.resize(desc.inputTensors.size());
    model.outputs.resize(desc.outputTensors.size());

    for (size_t i = 0; i < desc.inputTensors.size(); i++) {
        const pek::TensorDescriptor &tensor = desc.inputTensors[i];
        if (!inputNames[i] || std::string(inputNames[i]).empty()) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("NCNN input tensor {} has no runtime blob name", i))};
        }

        pek::ModelInput &input = model.inputs[i];
        input.name = inputNames[i];
        input.valueType = tensor.dtype;
        input.shape = tensor.shape;
        input.batch = (input.shape.rank > 0) ? input.shape.dims[0] : 0;

        if (pek::isScalarDataKind(tensor.dataKind)) {
            const size_t count = scalarValueCount(tensor.dataKind);
            input.shape = pek::Shape(static_cast<int>(count));
            input.batch = 1;
        }
    }

    for (size_t i = 0; i < desc.outputTensors.size(); i++) {
        const pek::TensorDescriptor &tensor = desc.outputTensors[i];
        if (!outputNames[i] || std::string(outputNames[i]).empty()) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("NCNN output tensor {} has no runtime blob name", i))};
        }

        pek::ModelOutput &output = model.outputs[i];
        output.name = outputNames[i];
        output.valueType = tensor.dtype;
        output.shape = tensor.shape;
    }

    return model;
}

pek::Result<void> Inference::setup(const pek::ModelDescriptor &modelDesc) {
    modelDescriptor = modelDesc;
    paramPath = modelDescriptor.modelFile;
    binPath = deriveBinPath(paramPath);

    unsigned int hwThreads = std::thread::hardware_concurrency();
    unsigned int threadCount = hwThreads ? std::max<unsigned int>(1u, hwThreads - 1u) : 1u;
    net.opt.num_threads = static_cast<int>(threadCount);
    net.opt.use_vulkan_compute = false;

    int paramResult = net.load_param(paramPath.c_str());
    if (paramResult != 0) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InferenceRtModelLoadError,
            fmt::format("failed to load NCNN param file '{}': {}", paramPath, paramResult))};
    }

    int binResult = net.load_model(binPath.c_str());
    if (binResult != 0) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InferenceRtModelLoadError,
                      fmt::format("failed to load NCNN model file '{}': {}", binPath, binResult))};
    }

    auto modelResult = buildModelFromDescriptor(modelDescriptor, net);
    if (!modelResult) {
        return tl::unexpected{modelResult.error()};
    }

    model = *modelResult;

    pek::log::info("{}", pek::log::tools::enframe(model.toString(), "NCNN Model"));

    auto cmResult = model.applyModelFromDescriptor(modelDescriptor);
    if (!cmResult) {
        return tl::unexpected{cmResult.error()};
    }

    if (model.inputs.size() > pek::MaxTensorCount || model.outputs.size() > pek::MaxTensorCount) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InferenceRtModelLoadError,
                                        "NCNN model tensor count exceeds max supported")};
    }

    auto setTensorSizesResult = setTensorSizes();
    if (!setTensorSizesResult) {
        return tl::unexpected{setTensorSizesResult.error()};
    }

    setupReady = true;

    pek::log::info("{}", pek::log::tools::enframe(model.toString(), "Final Model"));
    pek::log::info("{}", "NCNN: Model loaded\n");

    return {};
}

pek::Result<void> Inference::setTensorSizes() {
    inputTensors.resize(model.inputs.size());
    outputTensors.resize(model.outputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        if (model.inputs[i].valueType != pek::Dtype::Float32) {
            return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData,
                                            fmt::format("NCNN input {} must be Float32", i))};
        }

        const size_t valueCount = model.inputs[i].shape.getFullValueCount();
        const size_t byteCount = valueCount * sizeof(float);
        inputTensors[i].resize(byteCount);
        std::fill(inputTensors[i].begin(), inputTensors[i].end(), 0);
        pek::log::info("NCNN input tensor prepared: {} bytes\n", byteCount);
    }

    for (size_t i = 0; i < model.outputs.size(); i++) {
        if (model.outputs[i].valueType != pek::Dtype::Float32) {
            return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData,
                                            fmt::format("NCNN output {} must be Float32", i))};
        }

        if (model.outputs[i].shape.isInvalid()) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("NCNN output {} must have a static shape", i))};
        }

        const size_t byteCount = model.outputs[i].shape.getFullValueCount() * sizeof(float);
        outputTensors[i].resize(byteCount);
        std::fill(outputTensors[i].begin(), outputTensors[i].end(), 0);
        pek::log::info("NCNN output tensor prepared: {} bytes\n", byteCount);
    }

    return {};
}

pek::Result<void> Inference::setScalarInputValues() {
    for (size_t i = 0; i < model.inputs.size(); i++) {
        if (!pek::isScalarDataKind(model.inputs[i].dataKind)) {
            continue;
        }

        const size_t valueCount = scalarValueCount(model.inputs[i].dataKind);
        if (model.inputs[i].valueInputs.size() != valueCount) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("NCNN scalar input {} value count mismatch", i))};
        }

        std::memcpy(
            inputTensors[i].data(), model.inputs[i].valueInputs.data(), valueCount * sizeof(float));
    }

    return {};
}

pek::Result<ncnn::Mat> Inference::createInputMat(size_t tensorIndex) const {
    const pek::ModelInput &input = model.inputs[tensorIndex];
    const pek::Shape &shape = input.shape;
    const float *data = reinterpret_cast<const float *>(inputTensors[tensorIndex].data());

    if (input.valueType != pek::Dtype::Float32) {
        return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        fmt::format("NCNN input {} must be Float32", tensorIndex))};
    }

    if (shape.isInvalid()) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("NCNN input {} has invalid shape", tensorIndex))};
    }

    if (input.dataKind == pek::DataKind::ImageRgbChw ||
        input.dataKind == pek::DataKind::ImageGray) {
        int channels = 0;
        int height = 0;
        int width = 0;

        if (shape.rank == 4) {
            if (requirePositiveDim(shape, 0) != 1) {
                return tl::unexpected{
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              "NCNN image inputs currently require batch size 1")};
            }
            channels = requirePositiveDim(shape, 1);
            height = requirePositiveDim(shape, 2);
            width = requirePositiveDim(shape, 3);
        } else if (shape.rank == 3) {
            channels = requirePositiveDim(shape, 0);
            height = requirePositiveDim(shape, 1);
            width = requirePositiveDim(shape, 2);
        } else {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData, "NCNN CHW image input must be rank 3 or 4")};
        }

        ncnn::Mat mat(width, height, channels, sizeof(float));
        copyDenseChwToMat(data, mat);
        return mat;
    }

    if (input.dataKind == pek::DataKind::ImageRgbHwc ||
        input.dataKind == pek::DataKind::ImageBgraHwc) {
        int channels = 0;
        int height = 0;
        int width = 0;

        if (shape.rank == 4) {
            if (requirePositiveDim(shape, 0) != 1) {
                return tl::unexpected{
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              "NCNN image inputs currently require batch size 1")};
            }
            height = requirePositiveDim(shape, 1);
            width = requirePositiveDim(shape, 2);
            channels = requirePositiveDim(shape, 3);
        } else if (shape.rank == 3) {
            height = requirePositiveDim(shape, 0);
            width = requirePositiveDim(shape, 1);
            channels = requirePositiveDim(shape, 2);
        } else {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData, "NCNN HWC image input must be rank 3 or 4")};
        }

        ncnn::Mat mat(width, height, channels, sizeof(float));
        copyDenseHwcToMat(data, mat);
        return mat;
    }

    if (shape.rank == 1) {
        ncnn::Mat mat(requirePositiveDim(shape, 0), sizeof(float));
        std::memcpy(mat.data, data, shape.getFullValueCount() * sizeof(float));
        return mat;
    }

    if (shape.rank == 2) {
        ncnn::Mat mat(requirePositiveDim(shape, 1), requirePositiveDim(shape, 0), sizeof(float));
        std::memcpy(mat.data, data, shape.getFullValueCount() * sizeof(float));
        return mat;
    }

    if (shape.rank == 3) {
        ncnn::Mat mat(requirePositiveDim(shape, 2),
                      requirePositiveDim(shape, 1),
                      requirePositiveDim(shape, 0),
                      sizeof(float));
        copyDenseChwToMat(data, mat);
        return mat;
    }

    if (shape.rank == 4) {
        ncnn::Mat mat(requirePositiveDim(shape, 3),
                      requirePositiveDim(shape, 2),
                      requirePositiveDim(shape, 1),
                      requirePositiveDim(shape, 0),
                      sizeof(float));
        copyDenseChwToMat(data, mat);
        return mat;
    }

    return tl::unexpected{
        PEK_ERROR(pek::ErrorFlag::InvalidData,
                  fmt::format("unsupported NCNN input tensor rank {}", shape.rank))};
}

pek::Result<void> Inference::copyOutputMat(size_t tensorIndex, const ncnn::Mat &mat) {
    const size_t actualValueCount = compactElementCount(mat);
    const size_t expectedValueCount = model.outputs[tensorIndex].shape.getFullValueCount();

    if (actualValueCount != expectedValueCount) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("NCNN output {} shape mismatch: model produced {}, descriptor "
                                  "expects {}",
                                  tensorIndex,
                                  shapeFromMat(mat).toString(),
                                  model.outputs[tensorIndex].shape.toString()))};
    }

    auto copyResult = copyMatToDenseBuffer(mat, outputTensors[tensorIndex]);
    if (!copyResult) {
        return tl::unexpected{copyResult.error()};
    }

    outputTensorPointers[tensorIndex] = outputTensors[tensorIndex].data();
    outputTensorFinalShapes[tensorIndex] = model.outputs[tensorIndex].shape;

    return {};
}

pek::Result<void> Inference::applyTensorFeedback() {
    for (const auto &feedback : model.tensorFeedbacks) {
        if (feedback.mode != pek::TensorFeedback::Mode::Copy) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData, "unsupported NCNN tensor feedback mode")};
        }

        size_t fromOutputIndex = feedback.fromOutputTensorIndex;
        size_t toInputIndex = feedback.toInputTensorIndex;

        if (fromOutputIndex >= outputTensors.size() || toInputIndex >= inputTensors.size()) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData, "NCNN tensor feedback index out of range")};
        }

        if (outputTensors[fromOutputIndex].size() != inputTensors[toInputIndex].size()) {
            return tl::unexpected{PEK_ERROR(pek::ErrorFlag::InvalidData,
                                            "NCNN tensor feedback buffer size mismatch")};
        }

        std::memcpy(inputTensors[toInputIndex].data(),
                    outputTensors[fromOutputIndex].data(),
                    inputTensors[toInputIndex].size());
    }

    return {};
}

pek::Result<void> Inference::inference() {
    if (!setupReady) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InferenceRtInferenceError, "NCNN model is not ready"));
    }

    auto scalarResult = setScalarInputValues();
    if (!scalarResult) {
        return tl::unexpected{scalarResult.error()};
    }

    for (size_t i = 0; i < pek::MaxTensorCount; i++) {
        outputTensorPointers[i] = nullptr;
        outputTensorFinalShapes[i] = pek::Shape();
    }

    ncnn::Extractor extractor = net.create_extractor();
    extractor.set_light_mode(true);

    std::vector<ncnn::Mat> inputMats;
    inputMats.reserve(model.inputs.size());

    for (size_t i = 0; i < model.inputs.size(); i++) {
        auto matResult = createInputMat(i);
        if (!matResult) {
            return tl::unexpected{matResult.error()};
        }

        inputMats.push_back(*matResult);
        int inputResult = extractor.input(model.inputs[i].name.c_str(), inputMats.back());
        if (inputResult != 0) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InferenceRtInferenceError,
                          fmt::format("NCNN failed to bind input '{}' at index {}: {}",
                                      model.inputs[i].name,
                                      i,
                                      inputResult))};
        }
    }

    for (size_t i = 0; i < model.outputs.size(); i++) {
        ncnn::Mat outputMat;
        int extractResult = extractor.extract(model.outputs[i].name.c_str(), outputMat);
        if (extractResult != 0) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InferenceRtInferenceError,
                          fmt::format("NCNN failed to extract output '{}' at index {}: {}",
                                      model.outputs[i].name,
                                      i,
                                      extractResult))};
        }

        auto copyResult = copyOutputMat(i, outputMat);
        if (!copyResult) {
            return tl::unexpected{copyResult.error()};
        }
    }

    auto feedbackResult = applyTensorFeedback();
    if (!feedbackResult) {
        return tl::unexpected{feedbackResult.error()};
    }

    return {};
}
