#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/model_io.h"
#include "uniflow/detection_types.h"

#include "OnnxTools.h"

#include <string>
#include <memory>

struct OnnxInference {

    OnnxInference();
    virtual ~OnnxInference();

    OnnxResult setup(const std::string& file);

    void setOutputParser(std::unique_ptr<uflw::NetworkOutputParser> parser) { this->outputParser = std::move(parser); }

    uflw::DetectionResult execute(const uflw::TensorReader* tensor0, const uflw::TensorReader* tensor1 = nullptr,
            const uflw::TensorReader* tensor2 = nullptr, const uflw::TensorReader* tensor3 = nullptr);

protected:

    std::string modelPath;

    bool setupReady = false;

    Ort::Env* environment = nullptr;
    Ort::Session* session = nullptr;
    Ort::SessionOptions* sessionOptions = nullptr;
    Ort::MemoryInfo* memoryInfo = nullptr;
    std::vector<char*> inputNames;
    std::vector<char*> outputNames;

    uflw::Model uflwModel;
    std::unique_ptr<uflw::NetworkOutputParser> outputParser;

};