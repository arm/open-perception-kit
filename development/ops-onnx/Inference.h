/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <cstdint>
#include <fmt/core.h>
#include <onnxruntime_cxx_api.h>

#include "amp/ModelDescriptor.h"

#include "amp/Model.h"
#include "amp/TensorView.h"
#include "amp/Types.h"

#include <memory>
#include <string>
#include <vector>

#include "amp/Result.h"

namespace onnx {

// ONNX level tensor
struct Tensor {

    Tensor(const amp::Shape &shape, amp::Tdt type) {
        this->shape = shape;
        this->type = type;
        this->typeByteSize = amp::getValueTypeByteSize(type);
        for (size_t i = 0; i < shape.dimensionCount; i++)
            onnxShape[i] = shape.valueCount[i];

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

    bool checkShape(const amp::Shape &shape) {
        return this->shape == shape;
    }

    Ort::Value createOnnxTensor(const Ort::MemoryInfo &memInfo) {
        if (this->type == amp::Tdt::Float32) {
            return Ort::Value::CreateTensor<float>(memInfo,
                                                   reinterpret_cast<float *>(getData()),
                                                   getElementCount(),
                                                   this->onnxShape,
                                                   this->shape.dimensionCount);
        } else if (this->type == amp::Tdt::Int64) {
            return Ort::Value::CreateTensor<int64_t>(memInfo,
                                                     reinterpret_cast<int64_t *>(getData()),
                                                     getElementCount(),
                                                     onnxShape,
                                                     this->shape.dimensionCount);
        } else {
            assert(0);
        }
    }

  private:
    amp::Tdt type;
    size_t typeByteSize;
    amp::Shape shape;
    int64_t onnxShape[8];

    std::vector<uint8_t> data;
};

struct Inference {

    Inference();
    virtual ~Inference();

    amp::Result<void> setupFromJson(const std::string &filePath);
    amp::Result<void> setup(const ModelDescriptor &modelDesc);
    bool isReady() {
        return setupReady;
    }

    amp::Result<void> preprocessImageData(size_t tensorIndex,
                                          const uint8_t *data,
                                          amp::DataKind dataKind,
                                          amp::Tdt valueType,
                                          size_t imageWidth,
                                          size_t imageHeight);

    // void prepareForPostprocess(amp::TensorParser::Input &input);
    amp::Result<void> inference();

    const amp::Model &getModel() const {
        return this->model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        return api.inputTensors[index]->getData();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);
        return outputTensorPointers[index];
    }

    amp::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  protected:
    amp::InferenceInfo inferenceInfo;
    bool setupReady = false;

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, amp::Tdt &outType);
    static std::vector<size_t>
    getTensorShape(const Ort::Session &session, amp::TensorInOut tensorInOut, int tensorIndex);
    static amp::Result<amp::Model> inspectModel(const Ort::Session &session);

    Ort::Env *environment = nullptr;
    Ort::SessionOptions *sessionOptions = nullptr;
    Ort::MemoryInfo *memoryInfo = nullptr;
    Ort::Session *session = nullptr;

    void setupTensorsForModel();
    void recreateInputTensor(size_t index, const amp::Shape &shape, amp::Tdt valueType);

    ModelDescriptor modelDescriptor;
    amp::Model model;

    std::vector<Ort::Value> dynamicOutputData;

    // Output tensor buffer pointers and shapes populated after inference().
    // Valid only until the next inference() call.
    // Backend internal: use TensorView interface (via getOutputTensorDataAddress) for external access.
    const uint8_t *outputTensorPointers[amp::MaxTensorCount] = {nullptr};
    amp::Shape outputTensorFinalShapes[amp::MaxTensorCount];

    // ---

    struct ApiTensorGlue {
        std::unique_ptr<onnx::Tensor> inputTensors[amp::MaxTensorCount];
        std::unique_ptr<onnx::Tensor> outputTensors[amp::MaxTensorCount];
        std::vector<const char *> inputNames;
        std::vector<const char *> outputNames;
        std::vector<Ort::Value> inputTensorVector;
        std::vector<Ort::Value> outputTensorVector;
    } api;
};
} // namespace onnx
