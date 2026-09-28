/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/
#pragma once

#include <cstdint>
#include <fmt/core.h>
#include <onnxruntime_cxx_api.h>

#include "Log.h"
#include "opk/ModelDescriptor.h"

#include "opk/Model.h"
#include "opk/Result.h"
#include "opk/Types.h"

#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace opk::onnx {

// ONNX level tensor
struct Tensor {

    Tensor(const opk::Shape &shape, opk::Dtype type) {
        this->shape = shape;
        this->type = type;
        this->typeByteSize = opk::getValueTypeByteSize(type);
        for (size_t i = 0; i < shape.rank; i++)
            onnxShape[i] = shape.dims[i];

        if (shape.hasDynamicDimension()) {
            opk::log::info("Creating dynamic tensor with shape: {}\n", shape.toString());
            // do nothing
        } else {
            this->data.resize(shape.getFullValueCount() * typeByteSize);
            std::fill(this->data.begin(), this->data.end(), 0);
        }
    }

    uint8_t *getData() {
        return (uint8_t *)data.data();
    }

    size_t getElementCount() {
        size_t elemCount = data.size() / typeByteSize;
        return elemCount;
    }

    size_t getByteCount() {
        return data.size();
    }

    bool checkShape(const opk::Shape &shape) {
        return this->shape == shape;
    }

    opk::Result<Ort::Value> createOnnxTensor(const Ort::MemoryInfo &memInfo) {
        try {
            if (this->type == opk::Dtype::Float32) {
                return Ort::Value::CreateTensor<float>(memInfo,
                                                       reinterpret_cast<float *>(getData()),
                                                       getElementCount(),
                                                       this->onnxShape,
                                                       this->shape.rank);
            }
            if (this->type == opk::Dtype::Int64) {
                return Ort::Value::CreateTensor<int64_t>(memInfo,
                                                         reinterpret_cast<int64_t *>(getData()),
                                                         getElementCount(),
                                                         onnxShape,
                                                         this->shape.rank);
            }
        } catch (const Ort::Exception &error) {
            const std::string message =
                "Failed to create ONNX tensor: " + std::string(error.what());
            opk::log::error("{}\n", message);
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::TensorError, message));
        }

        const std::string message =
            "Unsupported ONNX tensor dtype " + std::to_string(static_cast<int>(this->type));
        opk::log::error("{}\n", message);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::TensorError, message));
    }

  private:
    opk::Dtype type;
    size_t typeByteSize;
    opk::Shape shape;
    int64_t onnxShape[8];

    std::vector<uint8_t> data;
};

struct Inference {

    Inference();
    virtual ~Inference();

    opk::Result<void> setupFromJson(const std::string &filePath);
    opk::Result<void> setup(const opk::ModelDescriptor &modelDesc);
    bool isReady() {
        return setupReady;
    }

    opk::Result<void> preprocessImageData(size_t tensorIndex,
                                          const uint8_t *data,
                                          opk::DataKind dataKind,
                                          opk::Dtype valueType,
                                          size_t imageWidth,
                                          size_t imageHeight);

    // void prepareForPostprocess(opk::TensorParser::Input &input);
    opk::Result<void> inference();

    const opk::Model &getModel() const {
        return this->model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        return api.inputTensors[index]->getData();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);
        return outputTensorPointers[index];
    }

    opk::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  private:
    opk::InferenceInfo inferenceInfo;
    bool setupReady = false;

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, opk::Dtype &outType);
    static std::vector<int64_t>
    getTensorShape(const Ort::Session &session, opk::TensorInOut tensorInOut, int tensorIndex);
    static opk::Result<opk::Model> inspectModel(const Ort::Session &session);

    std::unique_ptr<Ort::Env> environment;
    std::unique_ptr<Ort::SessionOptions> sessionOptions;
    std::unique_ptr<Ort::MemoryInfo> memoryInfo;
    std::unique_ptr<Ort::Session> session;

    Result<void> setupTensorsForModel();
    Result<void> recreateInputTensor(size_t index, const opk::Shape &shape, opk::Dtype valueType);

    opk::ModelDescriptor modelDescriptor;
    opk::Model model;

    std::vector<Ort::Value> dynamicOutputData;

    // Output tensor buffer pointers and shapes populated after inference().
    // Valid only until the next inference() call.
    // Backend internal: use TensorView interface (via getOutputTensorDataAddress) for external
    // access.
    const uint8_t *outputTensorPointers[opk::MaxTensorCount] = {nullptr};
    opk::Shape outputTensorFinalShapes[opk::MaxTensorCount];

    // ---

    struct ApiTensorGlue {
        std::unique_ptr<onnx::Tensor> inputTensors[opk::MaxTensorCount];
        std::unique_ptr<onnx::Tensor> outputTensors[opk::MaxTensorCount];
        std::vector<const char *> inputNames;
        std::vector<const char *> outputNames;
        std::vector<Ort::Value> inputTensorVector;
        std::vector<Ort::Value> outputTensorVector;
    } api;
};
} // namespace opk::onnx
