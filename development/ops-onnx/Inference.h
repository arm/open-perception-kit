/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <cstdint>
#include <fmt/core.h>
#include <onnxruntime_cxx_api.h>

#include "pek/ModelDescriptor.h"

#include "pek/Model.h"
#include "pek/TensorView.h"
#include "pek/Types.h"

#include <memory>
#include <string>
#include <vector>

#include "pek/Result.h"

namespace pek::onnx {

// ONNX level tensor
struct Tensor {

    Tensor(const pek::Shape &shape, pek::Dtype type) {
        this->shape = shape;
        this->type = type;
        this->typeByteSize = pek::getValueTypeByteSize(type);
        for (size_t i = 0; i < shape.rank; i++)
            onnxShape[i] = shape.dims[i];

        if (shape.hasDynamicDimension()) {
            fmt::print("Creating dynamic tensor with shape: {}\n", shape.toString());
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

    bool checkShape(const pek::Shape &shape) {
        return this->shape == shape;
    }

    Ort::Value createOnnxTensor(const Ort::MemoryInfo &memInfo) {
        if (this->type == pek::Dtype::Float32) {
            return Ort::Value::CreateTensor<float>(memInfo,
                                                   reinterpret_cast<float *>(getData()),
                                                   getElementCount(),
                                                   this->onnxShape,
                                                   this->shape.rank);
        } else if (this->type == pek::Dtype::Int64) {
            return Ort::Value::CreateTensor<int64_t>(memInfo,
                                                     reinterpret_cast<int64_t *>(getData()),
                                                     getElementCount(),
                                                     onnxShape,
                                                     this->shape.rank);
        } else {
            assert(0);
        }
    }

  private:
    pek::Dtype type;
    size_t typeByteSize;
    pek::Shape shape;
    int64_t onnxShape[8];

    std::vector<uint8_t> data;
};

struct Inference {

    Inference();
    virtual ~Inference();

    pek::Result<void> setupFromJson(const std::string &filePath);
    pek::Result<void> setup(const pek::ModelDescriptor &modelDesc);
    bool isReady() {
        return setupReady;
    }

    pek::Result<void> preprocessImageData(size_t tensorIndex,
                                          const uint8_t *data,
                                          pek::DataKind dataKind,
                                          pek::Dtype valueType,
                                          size_t imageWidth,
                                          size_t imageHeight);

    // void prepareForPostprocess(pek::TensorParser::Input &input);
    pek::Result<void> inference();

    const pek::Model &getModel() const {
        return this->model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        return api.inputTensors[index]->getData();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);
        return outputTensorPointers[index];
    }

    pek::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  protected:
    pek::InferenceInfo inferenceInfo;
    bool setupReady = false;

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, pek::Dtype &outType);
    static std::vector<size_t>
    getTensorShape(const Ort::Session &session, pek::TensorInOut tensorInOut, int tensorIndex);
    static pek::Result<pek::Model> inspectModel(const Ort::Session &session);

    Ort::Env *environment = nullptr;
    Ort::SessionOptions *sessionOptions = nullptr;
    Ort::MemoryInfo *memoryInfo = nullptr;
    Ort::Session *session = nullptr;

    void setupTensorsForModel();
    void recreateInputTensor(size_t index, const pek::Shape &shape, pek::Dtype valueType);

    pek::ModelDescriptor modelDescriptor;
    pek::Model model;

    std::vector<Ort::Value> dynamicOutputData;

    // Output tensor buffer pointers and shapes populated after inference().
    // Valid only until the next inference() call.
    // Backend internal: use TensorView interface (via getOutputTensorDataAddress) for external
    // access.
    const uint8_t *outputTensorPointers[pek::MaxTensorCount] = {nullptr};
    pek::Shape outputTensorFinalShapes[pek::MaxTensorCount];

    // ---

    struct ApiTensorGlue {
        std::unique_ptr<onnx::Tensor> inputTensors[pek::MaxTensorCount];
        std::unique_ptr<onnx::Tensor> outputTensors[pek::MaxTensorCount];
        std::vector<const char *> inputNames;
        std::vector<const char *> outputNames;
        std::vector<Ort::Value> inputTensorVector;
        std::vector<Ort::Value> outputTensorVector;
    } api;
};
} // namespace pek::onnx
