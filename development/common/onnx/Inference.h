#pragma once

#include <onnxruntime_cxx_api.h>

#include "ModelDescriptor.h"
#include "uniflow/model_io.h"
#include "uniflow/detection_types.h"

#include "onnx/Tools.h"

#include "onnx/Tensor.h"

#include "uniflow/public_types.h"
#include "uniflow/tensor_view.h"

#include <string>
#include <memory>
#include <vector>

#include "amp/Result.h"

namespace onnx {

struct Inference {

    Inference();
    virtual ~Inference();

    amp::Result<void> setupFromJson(const std::string& filePath);
    amp::Result<void> setup(const ModelDescriptor& modelDesc);

    void setOutputParser(std::unique_ptr<uflw::NetworkOutputParser> parser) { this->outputParser = std::move(parser); }
    void setInputBuilder(std::unique_ptr<uflw::NetworkInputBuilder> builder) { this->inputBuilder = std::move(builder); }

    onnx::Result preprocessImageData(size_t tensorIndex, const uint8_t* data, uflw::TensorDataKind dataKind, uflw::ValueType valueType, size_t imageWidth, size_t imageHeight);
    onnx::Result inference();
    onnx::Result postprocess(const uflw::NetworkOutputParser::Settings& settings, uflw::DetectionResult& outDetectionResults);

    const uflw::Model& getModel() const { return this->model; }

protected:

    uflw::NetworkOutputParser::InferenceMetadata inferenceMetaData;
    bool setupReady = false;

    Ort::Env* environment = nullptr;
    Ort::Session* session = nullptr;
    Ort::SessionOptions* sessionOptions = nullptr;
    Ort::MemoryInfo* memoryInfo = nullptr;

    void setupTensorsForModel();

    ModelDescriptor modelDescriptor;
    uflw::Model model;
    std::unique_ptr<uflw::NetworkOutputParser> outputParser;
    std::unique_ptr<uflw::NetworkInputBuilder> inputBuilder;

    bool useDynamicOutput = true;
    std::vector<Ort::Value> dynamicOutputData;
    std::unique_ptr<uflw::TensorReader> outputTensorReaders[4];

    // ---

    struct ApiTensorGlue {
        std::unique_ptr<onnx::Tensor> inputTensors[4];
        std::unique_ptr<onnx::Tensor> outputTensors[4];
        std::vector<const char*> inputNames;
        std::vector<const char*> outputNames;
        std::vector<Ort::Value> inputTensorVector;
        std::vector<Ort::Value> outputTensorVector;
    };
    ApiTensorGlue api;

};}


