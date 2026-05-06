/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "Inference.h"

#include "amp/Log.h"
#include "amp/Perception.h"
#include "amp/Result.h"
#include "amp/Shape.h"
#include "amp/String.h"

#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

#include <fmt/core.h>

#include "amp/Result.h"
#include "amp/String.h"
#include "amp/Types.h"

#include "magic_enum/magic_enum.hpp"

#include "amp/ModelDescriptor.h"

using namespace onnx;

Inference::Inference() {}

Inference::~Inference() {
    if (this->sessionOptions)
        delete this->sessionOptions;

    if (this->environment)
        delete this->environment;

    if (this->memoryInfo)
        delete this->memoryInfo;

    if (this->session)
        delete this->session;
}

amp::Result<void> Inference::setupFromJson(const std::string &filePath) {

    auto descResult = ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }

    { // setup model file name
        std::string modelRoot = filePath;
        if (amp::utf8::contains(modelRoot, '/')) {
            size_t lastSlashAt = amp::utf8::lastIndexOf(modelRoot, '/');
            modelRoot = amp::utf8::left(modelRoot, lastSlashAt + 1);
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

amp::Result<void> Inference::setup(const ModelDescriptor &modelDesc_) {

    this->api = ApiTensorGlue();
    this->modelDescriptor = modelDesc_;
    // this->modelPath = file;

    try {
        unsigned int hwThreads = std::thread::hardware_concurrency();
        // IntraOp multithreading seems to be a better choice for vision models
        unsigned int intraThreads = hwThreads ? std::max<unsigned int>(1u, hwThreads - 1u) : 1u;
        unsigned int interThreads = 1u;

        this->sessionOptions = new Ort::SessionOptions();
        this->sessionOptions->SetIntraOpNumThreads(intraThreads);
        this->sessionOptions->SetInterOpNumThreads(interThreads);
        this->sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        this->sessionOptions->EnableCpuMemArena();
        this->sessionOptions->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        this->sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // CPU threading
        // this->sessionOptions->SetIntraOpNumThreads(12);   // try 6/8/10/12 on M4
        // this->sessionOptions->SetInterOpNumThreads(1);
        // this->sessionOptions->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        // ---

        this->environment = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
        this->memoryInfo =
            new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));

        this->session = new Ort::Session(
            *this->environment, modelDescriptor.modelFile.c_str(), *this->sessionOptions);

        auto modelResult = inspectModel(*this->session);
        if (!modelResult) {
            return tl::unexpected{modelResult.error()};
        }
        this->model = *modelResult;
        this->model.modelFamily = modelDesc_.modelFamily;

        // --- build up model

        amp::log("{}", amp::LogTools::enframe(model.toString(), "ONNX Model"));

        auto cmResult = model.applyModelFromDescriptor(this->modelDescriptor);
        if (!cmResult) {
            return tl::make_unexpected(cmResult.error());
        }

        this->setupTensorsForModel();

        this->setupReady = true;

        amp::log("{}", amp::LogTools::enframe(model.toString(), "Final Model"));
        amp::log("{}", "ONNX: Model loaded\n");

    } catch (const std::exception &e) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::OnnxModelLoadException, e.what()));
    }

    return {};
}

void Inference::recreateInputTensor(size_t index, const amp::Shape &shape, amp::Tdt valueType) {
    fmt::print("Recreating input tensor #{} [{}] from {} to {}\n",
               index,
               this->model.inputs[index].name,
               this->model.inputs[index].shape.toString(),
               shape.toString());

    api.inputTensors[index] = std::make_unique<onnx::Tensor>(shape, valueType);
    api.inputTensorVector[index] = api.inputTensors[index]->createOnnxTensor(*this->memoryInfo);
}

void Inference::setupTensorsForModel() {

    for (size_t i = 0; i < this->model.inputs.size(); i++) {
        fmt::print("Setting up input tensor #{} [{}] with shape: {}\n",
                   i,
                   this->model.inputs[i].name,
                   this->model.inputs[i].shape.toString().c_str());

        api.inputTensors[i] = std::make_unique<onnx::Tensor>(this->model.inputs[i].shape,
                                                             this->model.inputs[i].valueType);
        api.inputNames.push_back(this->model.inputs[i].name.c_str());
        api.inputTensorVector.push_back(api.inputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

    for (size_t i = 0; i < this->model.outputs.size(); i++) {
        fmt::print("Setting up output tensor #{} [{}] with shape: {}\n",
                   i,
                   this->model.outputs[i].name,
                   this->model.outputs[i].shape.toString().c_str());

        api.outputTensors[i] = std::make_unique<onnx::Tensor>(this->model.outputs[i].shape,
                                                              this->model.outputs[i].valueType);
        api.outputNames.push_back(this->model.outputs[i].name.c_str());
        if (false == model.useDynamicOutput)
            api.outputTensorVector.push_back(
                api.outputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

    amp::log("ONNX: Input tensors are set up\n");
}

template <typename toT, typename fromT>
void writeValueTo(void *ptr, size_t valueIndex, void *valueAddress) {
    toT *address = (toT *)ptr;
    address[valueIndex] = *(fromT *)valueAddress;
}

amp::Result<void> Inference::inference() {

    // setting scalar tensors
    for (size_t i = 0; i < api.inputTensorVector.size(); i++) {
        if (amp::isScalarDataKind(this->model.inputs[i].dataKind)) {

            size_t valueCount = 0;
            if (this->model.inputs[i].dataKind == amp::DataKind::Value)
                valueCount = 1;
            if (this->model.inputs[i].dataKind == amp::DataKind::Vector2)
                valueCount = 2;
            if (this->model.inputs[i].dataKind == amp::DataKind::Vector3)
                valueCount = 3;
            if (this->model.inputs[i].dataKind == amp::DataKind::Vector4)
                valueCount = 4;

            if (this->model.inputs[i].valueType == amp::Tdt::Float32) {
                for (size_t g = 0; g < valueCount; g++) {
                    writeValueTo<float, float>(api.inputTensors[i]->getData(),
                                               g,
                                               this->model.inputs[i].valueInputs.data());
                }
            } else {
                assert(0); // no type support to set scalar tensor input value
            }
        }
    }

    // run the inference
    try {
        if (false == model.useDynamicOutput) {
            this->session->Run(Ort::RunOptions{nullptr},
                               (const char *const *)this->api.inputNames.data(),
                               api.inputTensorVector.data(),
                               api.inputTensorVector.size(),
                               (const char *const *)api.outputNames.data(),
                               api.outputTensorVector.data(),
                               api.outputTensorVector.size());
        } else {
            dynamicOutputData = this->session->Run(Ort::RunOptions{nullptr},
                                                   (const char *const *)api.inputNames.data(),
                                                   api.inputTensorVector.data(),
                                                   api.inputTensorVector.size(),
                                                   (const char *const *)api.outputNames.data(),
                                                   api.outputNames.size());
        }
    } catch (const std::exception &e) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::OnnxInferenceException, e.what()));
    }

    // fill the tensors data pointers
    // postprocessor will use these addresses
    for (size_t i = 0; i < amp::MaxTensorCount; i++)
        outputTensorPointers[i] = nullptr;

    if (false == model.useDynamicOutput) {
        if (api.outputTensorVector.size() > amp::MaxTensorCount) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData,
                          fmt::format("Model output tensor count {} exceeds max supported {}",
                                      api.outputTensorVector.size(),
                                      amp::MaxTensorCount)));
        }
        for (size_t i = 0; i < api.outputTensorVector.size(); i++) {
            outputTensorPointers[i] = api.outputTensorVector[i].GetTensorData<uint8_t>();
        }
    } else {
        if (dynamicOutputData.size() > amp::MaxTensorCount) {
            return tl::make_unexpected(AMP_ERROR(
                amp::ErrorFlag::InvalidData,
                fmt::format("Model dynamic output tensor count {} exceeds max supported {}",
                            dynamicOutputData.size(),
                            amp::MaxTensorCount)));
        }
        for (size_t i = 0; i < dynamicOutputData.size(); i++) {
            Ort::Value &v = dynamicOutputData[i];
            outputTensorPointers[i] = v.GetTensorMutableData<uint8_t>();
        }
    }

    // fill the final tensor shapes
    // postprocessor will use these shapes
    for (size_t i = 0; i < amp::MaxTensorCount; i++)
        outputTensorFinalShapes[i] = amp::Shape();
    if (false == model.useDynamicOutput) {
        for (size_t i = 0; i < api.outputTensorVector.size(); i++) {
            std::vector<size_t> onnxShape =
                getTensorShape(*this->session, amp::TensorInOut::Out, i);
            outputTensorFinalShapes[i].setFrom(onnxShape);
        }
    } else {
        for (size_t i = 0; i < dynamicOutputData.size(); i++) {
            Ort::Value &v = dynamicOutputData[i];
            auto tinfo = v.GetTensorTypeAndShapeInfo();
            std::vector<int64_t> onnxShape = tinfo.GetShape();
            outputTensorFinalShapes[i].setFrom(onnxShape);
        }
    }

    // realloc input tensors if matchShapeOutputIndex is set
    if (model.useDynamicOutput) {
        for (size_t i = 0; i < model.inputs.size(); i++) {
            if (model.inputs[i].matchShapeOutputIndex != amp::InvalidTensorIndex) {
                size_t outputIndex = model.inputs[i].matchShapeOutputIndex;
                if (outputIndex < model.outputs.size()) {
                    const amp::Shape &outputShape = outputTensorFinalShapes[outputIndex];
                    amp::Tdt valueType = model.inputs[i].valueType;
                    fmt::print(
                        "Reallocating input tensor #{} to match output tensor #{} shape: {}\n",
                        i,
                        outputIndex,
                        outputShape.toString().c_str());
                    recreateInputTensor(i, outputShape, valueType);
                }
                model.inputs[i].matchShapeOutputIndex = amp::InvalidTensorIndex;
            }
        }
    }

    // tensor feedback
    if (model.tensorFeedbacks.size()) {
        size_t tesorIndex = 0;
        for (const auto &feedback : model.tensorFeedbacks) {
            if (feedback.mode == amp::TensorFeedback::Mode::Copy) {
                size_t fromOutputIndex = feedback.fromOutputTensorIndex;
                size_t toInputIndex = feedback.toInputTensorIndex;

                if (fromOutputIndex >= model.outputs.size() ||
                    toInputIndex >= model.inputs.size()) {
                    return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                                         "tensor feedback index out of range"));
                }

                size_t fromByteCount = 0;
                if (model.useDynamicOutput) {
                    Ort::Value &v = dynamicOutputData[fromOutputIndex];
                    auto tinfo = v.GetTensorTypeAndShapeInfo();
                    size_t valueCount = 1;
                    for (auto d : tinfo.GetShape())
                        valueCount *= d;
                    fromByteCount = valueCount * amp::getValueTypeByteSize(
                                                     model.outputs[fromOutputIndex].valueType);
                } else {
                    fromByteCount = api.outputTensors[fromOutputIndex]->getByteCount();
                }

                size_t toByteCount = api.inputTensors[toInputIndex]->getByteCount();

                if (fromByteCount != toByteCount) {
                    return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                                         "tensor feedback buffer size mismatch"));
                }

                memcpy(api.inputTensors[toInputIndex]->getData(),
                       outputTensorPointers[fromOutputIndex],
                       toByteCount);

                tesorIndex++;
            } else {
                assert(0); // unsupported feedback mode
            }
        }
    }

    return {};
}

std::vector<size_t> Inference::getTensorShape(const Ort::Session &session,
                                              amp::TensorInOut tensorInOut,
                                              int tensorIndex) {
    Ort::TypeInfo ti = (tensorInOut == amp::TensorInOut::In)
                           ? session.GetInputTypeInfo(tensorIndex)
                           : session.GetOutputTypeInfo(tensorIndex);
    auto tensor = ti.GetTensorTypeAndShapeInfo();
    std::vector<size_t> dims;
    for (const auto &a : tensor.GetShape())
        dims.push_back(a);
    return dims;
}

amp::Result<amp::Model> Inference::inspectModel(const Ort::Session &session) {
    amp::Model model;
    model.engine = "onnxrt";

    Ort::AllocatorWithDefaultOptions allocator;
    model.inputs.resize(session.GetInputCount());
    model.outputs.resize(session.GetOutputCount());

    // inspect all the INPUT TENSORS
    for (size_t i = 0; i < model.inputs.size(); ++i) {
        Ort::TypeInfo ti = session.GetInputTypeInfo(i);

        auto tensor = ti.GetTensorTypeAndShapeInfo();
        // name
        model.inputs[i].name = session.GetInputNameAllocated(i, allocator).get();

        // tensor value type
        amp::Tdt tensorValueType;
        if (false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            fmt::format("cannot recognize input ONNX type: {}",
                                                        (uint64_t)tensor.GetElementType()))};
        }

        if (amp::Tdt::Float32 != tensorValueType && amp::Tdt::Int64 != tensorValueType) {
            return tl::unexpected{
                AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                          "only float32 or int64 input tensors are supported in ONNX")};
        }
        model.inputs[i].valueType = tensorValueType;

        // shape
        std::vector<size_t> onnxDims = getTensorShape(session, amp::TensorInOut::In, i);
        if (onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            "input tensor size must be between 1 and 8")};
        }
        model.inputs[i].shape.setFrom(onnxDims);
    }

    // inspect all the OUTPUT TENSORS
    for (size_t i = 0; i < model.outputs.size(); ++i) {
        Ort::TypeInfo ti = session.GetOutputTypeInfo(i);

        auto tensor = ti.GetTensorTypeAndShapeInfo();
        // name
        model.outputs[i].name = session.GetOutputNameAllocated(i, allocator).get();

        // tensor value type
        amp::Tdt tensorValueType;
        if (false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            fmt::format("cannot recognize output ONNX type: {}",
                                                        (uint64_t)tensor.GetElementType()))};
        }

        if (amp::Tdt::Float32 != tensorValueType && amp::Tdt::Int64 != tensorValueType &&
            amp::Tdt::Float16 != tensorValueType) {
            return tl::unexpected{
                AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                          "only float16, float32 or int64 input tensors are supported in ONNX")};
        }
        model.outputs[i].valueType = tensorValueType;

        // shape
        std::vector<size_t> onnxDims = getTensorShape(session, amp::TensorInOut::Out, i);
        if (onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            "output tensor size must be between 1 and 8")};
        }
        model.outputs[i].shape.setFrom(onnxDims);
    }

    return model;
}

bool Inference::onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, amp::Tdt &outType) {
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        outType = amp::Tdt::Float32;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        outType = amp::Tdt::Float16;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
        outType = amp::Tdt::Int8;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
        outType = amp::Tdt::Uint8;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        outType = amp::Tdt::Int64;
        return true;
    }
    return false;
}
