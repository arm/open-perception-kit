#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/model_io.h"
#include "uniflow/detection_types.h"

#include "OnnxTools.h"
#include "uniflow/public_types.h"
#include "uniflow/tensor_view.h"

#include <string>
#include <memory>
#include <vector>

#include "amp/Result.h"

struct OnnxInference {

    OnnxInference();
    virtual ~OnnxInference();

    OnnxResult setup(const std::string& file);

    void setOutputParser(std::unique_ptr<uflw::NetworkOutputParser> parser) { this->outputParser = std::move(parser); }
    void setInputBuilder(std::unique_ptr<uflw::NetworkInputBuilder> builder) { this->inputBuilder = std::move(builder); }

    OnnxResult preprocessImageData(size_t tensorIndex, const uint8_t* data, uflw::TensorDataKind dataKind, uflw::ValueType valueType, size_t imageWidth, size_t imageHeight);
    OnnxResult inference();
    OnnxResult postprocess(const uflw::NetworkOutputParser::Settings& settings, uflw::DetectionResult& outDetectionResults);

    const uflw::Model& getModel() const { return *this->model; }

protected:

    uflw::NetworkOutputParser::InferenceMetadata inferenceMetaData;

    std::string modelPath;
    bool setupReady = false;

    Ort::Env* environment = nullptr;
    Ort::Session* session = nullptr;
    Ort::SessionOptions* sessionOptions = nullptr;
    Ort::MemoryInfo* memoryInfo = nullptr;

    void setupTensorsForModel();

    std::unique_ptr<uflw::Model> model;
    std::unique_ptr<uflw::NetworkOutputParser> outputParser;
    std::unique_ptr<uflw::NetworkInputBuilder> inputBuilder;
    std::unique_ptr<OnnxTensor> inputTensors[4];
    std::unique_ptr<OnnxTensor> outputTensors[4];
    std::vector<const char*> inputNames;
    std::vector<const char*> outputNames;
    std::vector<Ort::Value> inputTensorVector;
    std::vector<Ort::Value> outputTensorVector;

    bool useDynamicOutput = true;
    std::vector<Ort::Value> dynamicOutputData;
    std::unique_ptr<uflw::TensorReader> outputTensorReaders[4];
};


