/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "Inference.h"

#include "Log.h"
#include "opk/Result.h"
#include "opk/Shape.h"
#include "tools.h"

#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>

#include <fmt/core.h>

#include "opk/Result.h"
#include "opk/Types.h"

#include "magic_enum/magic_enum.hpp"

#include "opk/ModelDescriptor.h"

using namespace opk::onnx;

Inference::Inference() = default;

Inference::~Inference() = default;

opk::Result<void> Inference::setupFromJson(const std::string &filePath) {
    auto descResult = opk::ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }
    return setup(*descResult);
}

opk::Result<void> Inference::setup(const opk::ModelDescriptor &modelDesc_) {

    this->setupReady = false;
    this->api = ApiTensorGlue();
    this->dynamicOutputData.clear();
    this->model = {};
    std::fill_n(this->outputTensorPointers, opk::MaxTensorCount, nullptr);
    std::fill_n(this->outputTensorFinalShapes, opk::MaxTensorCount, opk::Shape{});
    this->session.reset();
    this->memoryInfo.reset();
    this->sessionOptions.reset();
    this->environment.reset();

    this->modelDescriptor = modelDesc_;
    // this->modelPath = file;

    try {
        unsigned int hwThreads = std::thread::hardware_concurrency();
        // IntraOp multithreading seems to be a better choice for vision models
        unsigned int intraThreads = hwThreads ? std::max<unsigned int>(1u, hwThreads - 1u) : 1u;
        unsigned int interThreads = 1u;

        this->sessionOptions = std::make_unique<Ort::SessionOptions>();
        this->sessionOptions->SetLogSeverityLevel(static_cast<int>(ORT_LOGGING_LEVEL_ERROR));
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

        this->environment.reset(new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "opkinfer"));
        this->memoryInfo = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));

        this->session = std::make_unique<Ort::Session>(
            *this->environment, modelDescriptor.modelFile.c_str(), *this->sessionOptions);

        auto modelResult = inspectModel(*this->session);
        if (!modelResult) {
            return tl::unexpected{modelResult.error()};
        }
        this->model = *modelResult;

        // --- build up model

        opk::log::info("{}", opk::log::tools::enframe(model.toString(), "ONNX Model"));

        auto cmResult = model.applyModelFromDescriptor(this->modelDescriptor);
        if (!cmResult) {
            return tl::unexpected(cmResult.error());
        }

        auto setupTensorsResult = this->setupTensorsForModel();
        if (!setupTensorsResult) {
            return tl::unexpected{setupTensorsResult.error()};
        }

        this->setupReady = true;

        opk::log::info("{}", opk::log::tools::enframe(model.toString(), "Final Model"));
        opk::log::info("{}", "ONNX: Model loaded\n");

    } catch (const std::exception &e) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtModelLoadError, e.what()));
    }

    return {};
}

opk::Result<void>
Inference::recreateInputTensor(size_t index, const opk::Shape &shape, opk::Dtype valueType) {
    if (index >= opk::MaxTensorCount) {
        opk::log::error(
            "Input tensor index {} exceeds max supported {}\n", index, opk::MaxTensorCount);
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "Input tensor index exceeds max supported"));
    }

    opk::log::info("Recreating input tensor #{} [{}] from {} to {}\n",
                   index,
                   this->model.inputs[index].name,
                   this->model.inputs[index].shape.toString(),
                   shape.toString());

    api.inputTensors[index] = std::make_unique<onnx::Tensor>(shape, valueType);
    auto tensorResult = api.inputTensors[index]->createOnnxTensor(*this->memoryInfo);
    if (!tensorResult) {
        return tl::unexpected(tensorResult.error());
    }
    api.inputTensorVector[index] = std::move(*tensorResult);
    return {};
}

opk::Result<void> Inference::setupTensorsForModel() {

    if (this->model.inputs.size() > opk::MaxTensorCount) {
        opk::log::error("Model input tensor count {} exceeds max supported {}\n",
                        this->model.inputs.size(),
                        opk::MaxTensorCount);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtModelLoadError,
                                        "Model input tensor count exceeds max supported"));
    }

    for (size_t i = 0; i < this->model.inputs.size(); i++) {
        opk::log::info("Setting up input tensor #{} [{}] with shape: {}\n",
                       i,
                       this->model.inputs[i].name,
                       this->model.inputs[i].shape.toString());

        api.inputTensors[i] = std::make_unique<onnx::Tensor>(this->model.inputs[i].shape,
                                                             this->model.inputs[i].valueType);
        api.inputNames.push_back(this->model.inputs[i].name.c_str());
        auto tensorResult = api.inputTensors[i]->createOnnxTensor(*this->memoryInfo);
        if (!tensorResult) {
            return tl::unexpected(tensorResult.error());
        }
        api.inputTensorVector.push_back(std::move(*tensorResult));
    }

    if (this->model.outputs.size() > opk::MaxTensorCount) {
        opk::log::error("Model output tensor count {} exceeds max supported {}\n",
                        this->model.outputs.size(),
                        opk::MaxTensorCount);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtModelLoadError,
                                        "Model output tensor count exceeds max supported"));
    }

    for (size_t i = 0; i < this->model.outputs.size(); i++) {
        opk::log::info("Setting up output tensor #{} [{}] with shape: {}\n",
                       i,
                       this->model.outputs[i].name,
                       this->model.outputs[i].shape.toString());

        api.outputTensors[i] = std::make_unique<onnx::Tensor>(this->model.outputs[i].shape,
                                                              this->model.outputs[i].valueType);
        api.outputNames.push_back(this->model.outputs[i].name.c_str());
        if (false == model.useDynamicOutput) {
            auto tensorResult = api.outputTensors[i]->createOnnxTensor(*this->memoryInfo);
            if (!tensorResult) {
                return tl::unexpected(tensorResult.error());
            }
            api.outputTensorVector.push_back(std::move(*tensorResult));
        }
    }

    opk::log::info("ONNX: Input tensors are set up\n");

    return {};
}

template <typename toT, typename fromT>
void writeValueTo(void *ptr, size_t valueIndex, void *valueAddress) {
    toT *address = (toT *)ptr;
    address[valueIndex] = *(fromT *)valueAddress;
}

opk::Result<void> Inference::inference() {

    // setting scalar tensors
    for (size_t i = 0; i < api.inputTensorVector.size(); i++) {
        if (opk::isScalarDataKind(this->model.inputs[i].dataKind)) {

            size_t valueCount = 0;
            if (this->model.inputs[i].dataKind == opk::DataKind::Value)
                valueCount = 1;
            if (this->model.inputs[i].dataKind == opk::DataKind::Vector2)
                valueCount = 2;
            if (this->model.inputs[i].dataKind == opk::DataKind::Vector3)
                valueCount = 3;
            if (this->model.inputs[i].dataKind == opk::DataKind::Vector4)
                valueCount = 4;

            if (this->model.inputs[i].valueType == opk::Dtype::Float32) {
                for (size_t g = 0; g < valueCount; g++) {
                    writeValueTo<float, float>(api.inputTensors[i]->getData(),
                                               g,
                                               this->model.inputs[i].valueInputs.data());
                }
            } else {
                opk::log::error("ONNX scalar input tensor {} has unsupported dtype {}\n",
                                i,
                                magic_enum::enum_name(this->model.inputs[i].valueType));
                return tl::unexpected(OPK_ERROR(
                    opk::ErrorFlag::InvalidData,
                    "ONNX scalar input tensor " + std::to_string(i) + " has unsupported dtype " +
                        std::string(magic_enum::enum_name(this->model.inputs[i].valueType))));
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
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtInferenceError, e.what()));
    }

    // fill the tensors data pointers
    // postprocessor will use these addresses
    for (size_t i = 0; i < opk::MaxTensorCount; i++)
        outputTensorPointers[i] = nullptr;

    if (false == model.useDynamicOutput) {
        if (api.outputTensorVector.size() > opk::MaxTensorCount) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("Model output tensor count {} exceeds max supported {}",
                                      api.outputTensorVector.size(),
                                      opk::MaxTensorCount)));
        }
        for (size_t i = 0; i < api.outputTensorVector.size(); i++) {
            outputTensorPointers[i] = api.outputTensorVector[i].GetTensorData<uint8_t>();
            if (!outputTensorPointers[i]) {
                opk::log::error("ONNX output tensor {} has no data\n", i);
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidData,
                              "ONNX output tensor " + std::to_string(i) + " has no data"));
            }
        }
    } else {
        if (dynamicOutputData.size() > opk::MaxTensorCount) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                fmt::format("Model dynamic output tensor count {} exceeds max supported {}",
                            dynamicOutputData.size(),
                            opk::MaxTensorCount)));
        }
        for (size_t i = 0; i < dynamicOutputData.size(); i++) {
            Ort::Value &v = dynamicOutputData[i];
            outputTensorPointers[i] = v.GetTensorMutableData<uint8_t>();
            if (!outputTensorPointers[i]) {
                opk::log::error("ONNX dynamic output tensor {} has no data\n", i);
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidData,
                              "ONNX dynamic output tensor " + std::to_string(i) + " has no data"));
            }
        }
    }

    // fill the final tensor shapes
    // postprocessor will use these shapes
    for (size_t i = 0; i < opk::MaxTensorCount; i++)
        outputTensorFinalShapes[i] = opk::Shape();
    if (false == model.useDynamicOutput) {
        for (size_t i = 0; i < api.outputTensorVector.size(); i++) {
            outputTensorFinalShapes[i] = model.outputs[i].shape;
        }
    } else {
        for (size_t i = 0; i < dynamicOutputData.size(); i++) {
            Ort::Value &v = dynamicOutputData[i];
            auto tinfo = v.GetTensorTypeAndShapeInfo();
            std::vector<int64_t> onnxShape = tinfo.GetShape();
            if (!outputTensorFinalShapes[i].setFrom(onnxShape)) {
                opk::log::error("ONNX dynamic output tensor {} size {} exceeds max supported 8\n",
                                i,
                                onnxShape.size());
                return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                                "ONNX dynamic output tensor " + std::to_string(i) +
                                                    " size " + std::to_string(onnxShape.size()) +
                                                    " exceeds max supported 8"));
            }
        }
    }

    // realloc input tensors if matchShapeOutputIndex is set
    if (model.useDynamicOutput) {
        for (size_t i = 0; i < model.inputs.size(); i++) {
            if (model.inputs[i].matchShapeOutputIndex != opk::InvalidTensorIndex) {
                size_t outputIndex = model.inputs[i].matchShapeOutputIndex;
                if (outputIndex < model.outputs.size()) {
                    const opk::Shape &outputShape = outputTensorFinalShapes[outputIndex];
                    opk::Dtype valueType = model.inputs[i].valueType;
                    opk::log::info(
                        "Reallocating input tensor #{} to match output tensor #{} shape: {}\n",
                        i,
                        outputIndex,
                        outputShape.toString());
                    auto recreateResult = recreateInputTensor(i, outputShape, valueType);
                    if (!recreateResult) {
                        return tl::unexpected(recreateResult.error());
                    }
                }
                model.inputs[i].matchShapeOutputIndex = opk::InvalidTensorIndex;
            }
        }
    }

    // tensor feedback
    if (model.tensorFeedbacks.size()) {
        size_t tesorIndex = 0;
        for (const auto &feedback : model.tensorFeedbacks) {
            if (feedback.mode == opk::TensorFeedback::Mode::Copy) {
                size_t fromOutputIndex = feedback.fromOutputTensorIndex;
                size_t toInputIndex = feedback.toInputTensorIndex;

                if (fromOutputIndex >= model.outputs.size() ||
                    toInputIndex >= model.inputs.size()) {
                    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                                    "tensor feedback index out of range"));
                }

                size_t fromByteCount = 0;
                if (model.useDynamicOutput) {
                    Ort::Value &v = dynamicOutputData[fromOutputIndex];
                    auto tinfo = v.GetTensorTypeAndShapeInfo();
                    size_t valueCount = 1;
                    for (auto d : tinfo.GetShape())
                        valueCount *= d;
                    fromByteCount = valueCount * opk::getValueTypeByteSize(
                                                     model.outputs[fromOutputIndex].valueType);
                } else {
                    fromByteCount = api.outputTensors[fromOutputIndex]->getByteCount();
                }

                size_t toByteCount = api.inputTensors[toInputIndex]->getByteCount();

                if (fromByteCount != toByteCount) {
                    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                                    "tensor feedback buffer size mismatch"));
                }

                memcpy(api.inputTensors[toInputIndex]->getData(),
                       outputTensorPointers[fromOutputIndex],
                       toByteCount);

                tesorIndex++;
            } else {
                opk::log::error("Unsupported tensor feedback mode\n");
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidData, "unsupported tensor feedback mode"));
            }
        }
    }

    return {};
}

std::vector<int64_t> Inference::getTensorShape(const Ort::Session &session,
                                               opk::TensorInOut tensorInOut,
                                               int tensorIndex) {
    Ort::TypeInfo ti = (tensorInOut == opk::TensorInOut::In)
                           ? session.GetInputTypeInfo(tensorIndex)
                           : session.GetOutputTypeInfo(tensorIndex);
    auto tensor = ti.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> dims;
    for (const auto &a : tensor.GetShape())
        dims.push_back(a);
    return dims;
}

opk::Result<opk::Model> Inference::inspectModel(const Ort::Session &session) {
    opk::Model model;
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
        opk::Dtype tensorValueType;
        if (false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                            fmt::format("cannot recognize input ONNX type: {}",
                                                        (uint64_t)tensor.GetElementType()))};
        }

        if (opk::Dtype::Float32 != tensorValueType && opk::Dtype::Int64 != tensorValueType) {
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                          "only float32 or int64 input tensors are supported in ONNX")};
        }
        model.inputs[i].valueType = tensorValueType;

        // shape
        std::vector<int64_t> onnxDims = getTensorShape(session, opk::TensorInOut::In, i);
        if (onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{OPK_ERROR(opk::ErrorFlag::ModelInspectError,
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
        opk::Dtype tensorValueType;
        if (false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                            fmt::format("cannot recognize output ONNX type: {}",
                                                        (uint64_t)tensor.GetElementType()))};
        }

        if (opk::Dtype::Float32 != tensorValueType && opk::Dtype::Int64 != tensorValueType &&
            opk::Dtype::Float16 != tensorValueType) {
            return tl::unexpected{
                OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                          "only float16, float32 or int64 input tensors are supported in ONNX")};
        }
        model.outputs[i].valueType = tensorValueType;

        // shape
        std::vector<int64_t> onnxDims = getTensorShape(session, opk::TensorInOut::Out, i);
        if (onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{OPK_ERROR(opk::ErrorFlag::ModelInspectError,
                                            "output tensor size must be between 1 and 8")};
        }
        model.outputs[i].shape.setFrom(onnxDims);
    }

    return model;
}

bool Inference::onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, opk::Dtype &outType) {
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        outType = opk::Dtype::Float32;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        outType = opk::Dtype::Float16;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
        outType = opk::Dtype::Int8;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
        outType = opk::Dtype::Uint8;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        outType = opk::Dtype::Int64;
        return true;
    }
    return false;
}
