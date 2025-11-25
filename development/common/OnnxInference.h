#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/model_io.h"

#include "OnnxTools.h"

#include <string>
#include <memory>

struct OnnxInference {

    OnnxInference();
    virtual ~OnnxInference();

    OnnxResult setup(const std::string& file);

    std::string modelPath;

protected:

    bool setupReady = false;

    Ort::Env* environment = nullptr;
    Ort::Session* session = nullptr;
    Ort::SessionOptions* sessionOptions = nullptr;
    Ort::MemoryInfo* memoryInfo = nullptr;
    std::vector<char*> inputNames;
    std::vector<char*> outputNames;

    uflw::Model managedModel;

};