#pragma once

#include <onnxruntime_cxx_api.h>

#include "ModelDescriptor.h"

#include "onnx/Tensor.h"

#include "amp/Model.h"
#include "amp/PerceptionContext.h"
#include "amp/Types.h"

#include "postproc/TensorParser.h"
#include "preproc/TensorBuilder.h"

#include <memory>
#include <string>
#include <vector>

#include "amp/Result.h"

namespace onnx {

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
                                          amp::TensorDataKind dataKind,
                                          amp::ValueType valueType,
                                          size_t imageWidth,
                                          size_t imageHeight);
    amp::Result<void> inference();
    amp::Result<void> postprocess(const amp::TensorParser::Settings &parserSettings,
                                  amp::DetectionResult &outDetectionResults);

    const amp::Model &getModel() const {
        return this->model;
    }

  protected:
    amp::TensorParser::InferenceInfo inferenceInfo;
    bool setupReady = false;

    Ort::Env *environment = nullptr;
    Ort::Session *session = nullptr;
    Ort::SessionOptions *sessionOptions = nullptr;
    Ort::MemoryInfo *memoryInfo = nullptr;

    void setupTensorsForModel();

    ModelDescriptor modelDescriptor;
    amp::Model model;
    std::unique_ptr<amp::TensorParser> outputParser;
    std::unique_ptr<amp::TensorBuilder> inputBuilder;

    bool useDynamicOutput = true;
    std::vector<Ort::Value> dynamicOutputData;
    std::unique_ptr<amp::TensorReader> outputTensorReaders[4];

    amp::Result<void> createModelFromModelDesc();
    amp::Result<void> createTensorProcessors();

    // ---

    struct ApiTensorGlue {
        std::unique_ptr<onnx::Tensor> inputTensors[4];
        std::unique_ptr<onnx::Tensor> outputTensors[4];
        std::vector<const char *> inputNames;
        std::vector<const char *> outputNames;
        std::vector<Ort::Value> inputTensorVector;
        std::vector<Ort::Value> outputTensorVector;
    } api;
};
} // namespace onnx
