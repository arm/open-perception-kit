#include "OnnxInference.h"
#include "OnnxTools.h"
#include "gst/video/video-enumtypes.h"
#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"
#include "uniflow/detection_types.h"
#include "uniflow/model_io.h"
#include "uniflow/tensor_view.h"
#include <memory>

#include <fmt/color.h>

#include "amp/Result.h"

OnnxInference::OnnxInference() {
}

OnnxInference::~OnnxInference() {

}

amp::Result<int> getShit() {    

    return tl::unexpected(AMP_ERROR(amp::ResultFlag::GenericError, "shit"));

}

OnnxResult OnnxInference::setup(const std::string& file) {

    if(auto r = getShit(); !r) {
        printf("%s\n", r.error().toString().c_str());
    } else {
        r = 4;
    }

    this->model = nullptr;
    this->inputBuilder = nullptr;
    this->outputParser = nullptr;
    for(size_t i = 0; i < 4; i++) {
        this->inputTensors[i] = nullptr;
        this->outputTensors[i] = nullptr;
    }
    this->inputNames.clear();
    this->outputNames.clear();

    this->inputTensorVector.clear();
    this->outputTensorVector.clear();

    // ---

    this->modelPath = file;

    try {
        
        this->sessionOptions = new Ort::SessionOptions();
        this->sessionOptions->SetIntraOpNumThreads(1);

        this->environment = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
        this->memoryInfo = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
  
        this->session = new Ort::Session(*this->environment, this->modelPath.c_str(), *this->sessionOptions);

        this->model = std::make_unique<uflw::Model>(OnnxTools::inspectModel(*this->session, this->modelPath));
        if(false == this->model->parseError.empty()) {
            printf("INSPECT MODEL FAILED: %s\n", this->model->parseError.c_str());
            return OnnxResult::UniflowModelInspectError;
        }
        
        std::string modelLog = OnnxTools::toString(*this->model);
        printf("---> New  model  parsed <---\n");
        printf("%s", modelLog.c_str());
        printf("--- --- --- ---- --- --- ---\n");

        setupTensorsForModel();

        this->setupReady = true;
    } 
    catch (const std::exception& e) {
    
        return OnnxResult::CreateEnvironmentError;
    }

    return OnnxResult::Ok;

}

void OnnxInference::setupTensorsForModel() {

    for(size_t i = 0; i < this->model->modelInputCount; i++) {
        this->inputTensors[i] = std::make_unique<OnnxTensor>(this->model->inputs[i].shape, this->model->inputs[i].valueType);
        this->inputNames.push_back(this->model->inputs[i].name.c_str());
        this->inputTensorVector.push_back(this->inputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

    for(size_t i = 0; i < this->model->modelOutputCount; i++) {
        this->outputTensors[i] = std::make_unique<OnnxTensor>(this->model->outputs[i].shape, this->model->outputs[i].valueType);
        this->outputNames.push_back(this->model->outputs[i].name.c_str());
        this->outputTensorVector.push_back(this->outputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

}

OnnxResult OnnxInference::preprocessImageData(size_t tensorIndex, const uint8_t* data, uflw::TensorDataKind dataKind, uflw::ValueType valueType, size_t imageWidth, size_t imageHeight) {

    uflw::NetworkInputBuilder::Setup setup;
    setup.original.data = data;
    setup.original.width = imageWidth;
    setup.original.height = imageHeight;
    setup.original.byteCount = imageWidth * imageHeight * 3;
    setup.original.kind = dataKind;
    setup.original.type = valueType;

    size_t modelWidth, modelHeight;
    model->inputs[tensorIndex].getImageWidthHeight(modelWidth, modelHeight);

    setup.target.data = inputTensors[tensorIndex]->getData();
    setup.target.byteCount = inputTensors[tensorIndex]->getByteCount();
    setup.target.width = modelWidth;
    setup.target.height = modelHeight;
    //setup.target.kind = uflw::TensorDataKind::ImageRgbChw;
    setup.target.kind = model->inputs[0].dataKind;
    setup.target.type = uflw::ValueType::f32;

    uflw::Result result = inputBuilder->build(setup);
    if(uflw::Result::Ok != result) {
      printf("ERROR!\n");
    }

    this->inferenceMetaData.image.width = imageWidth;
    this->inferenceMetaData.image.height = imageHeight;
    this->inferenceMetaData.image.modelWidth = modelWidth;
    this->inferenceMetaData.image.modelHeight = modelHeight;

    return OnnxResult::Ok;

}

OnnxResult OnnxInference::inference() {

    if(inputTensorVector.size() > 1) {
        *(float*)inputTensors[0]->getData() = 0.99f; // confidence
        *(int64_t*)inputTensors[1]->getData() = 1; // numdetections
        *(float*)inputTensors[2]->getData() = 0.9f; // iou threshold
    }

    if(false == this->useDynamicOutput) {
    this->session->Run(
        Ort::RunOptions { nullptr },
        (const char* const*)this->inputNames.data(),
        inputTensorVector.data(),
        inputTensorVector.size(),
        (const char* const*)this->outputNames.data(),
        outputTensorVector.data(),
        outputTensorVector.size());
    } else {
        dynamicOutputData = this->session->Run(
    Ort::RunOptions{nullptr},
    (const char* const*)this->inputNames.data(),
    inputTensorVector.data(),
    inputTensorVector.size(),
    (const char* const*)this->outputNames.data(),
    this->outputNames.size());
    }

    return OnnxResult::Ok;

}

OnnxResult OnnxInference::postprocess(const uflw::NetworkOutputParser::Settings& settings,
                                      uflw::DetectionResult& outDetectionResults) {

    if (!this->useDynamicOutput) {
        // --- STATIC, PREALLOCATED OUTPUTS ---

        const uflw::TensorReader* tensorReaders[4] = { nullptr, nullptr, nullptr, nullptr };

        for (size_t i = 0; i < 4; ++i) {
            if (i < model->modelOutputCount) {
                if (this->outputTensorReaders[i] == nullptr) {
                    this->outputTensorReaders[i] = std::make_unique<uflw::TensorReader>(
                        outputTensors[i]->getData(),
                        outputTensors[i]->getByteCount(),
                        model->outputs[i].shape,
                        model->outputs[i].valueType,
                        1.0f,
                        0.0f
                    );
                }
                tensorReaders[i] = this->outputTensorReaders[i].get();
            }
        }

        outputParser->parse(tensorReaders, settings, this->inferenceMetaData, outDetectionResults);
    } else {
        // --- DYNAMIC OUTPUTS ALLOCATED BY ORT ---

        const uflw::TensorReader* tensorReaders[4] = { nullptr, nullptr, nullptr, nullptr };

        // We’ll build temporary TensorReaders for this call only.
        // They just wrap ORT’s output buffers; no copying.
        std::vector<std::unique_ptr<uflw::TensorReader>> dynamicReaders;
        dynamicReaders.resize(4);

        const size_t numOutputs =
            std::min<size_t>(std::min<size_t>(model->modelOutputCount, dynamicOutputData.size()), 4);

        for (size_t i = 0; i < numOutputs; ++i) {
            Ort::Value& v = dynamicOutputData[i];

            auto tinfo = v.GetTensorTypeAndShapeInfo();
            auto onnxShape = tinfo.GetShape();
            ONNXTensorElementDataType elemType = tinfo.GetElementType();

            // Map ONNX type → uflw::ValueType
            uflw::ValueType valueType;
            if (!OnnxTools::onnxTypeToUniflowType(elemType, valueType)) {
                // If you have better error handling, plug it here
                assert(0);
            }

            // Build uflw::Shape from ORT shape
            uflw::Shape shape;
            shape.dimensionCount = onnxShape.size();
            for (size_t d = 0; d < shape.dimensionCount; ++d) {
                shape.valueCount[d] = static_cast<size_t>(onnxShape[d]);
            }

            // Compute byte count
            const size_t elemSize = uflw::getValueTypeByteSize(valueType);
            const size_t byteCount = elemSize * shape.getFullValueCount();

            // Get raw data pointer from ORT tensor
            void* dataPtr = v.GetTensorMutableData<void>();

            dynamicReaders[i] = std::make_unique<uflw::TensorReader>(
                dataPtr,
                byteCount,
                shape,
                valueType,
                1.0f,
                0.0f
            );

            tensorReaders[i] = dynamicReaders[i].get();
        }

        outputParser->parse(tensorReaders, settings, this->inferenceMetaData, outDetectionResults);
        // dynamicReaders stays alive until here, so tensorReaders are valid during parse()
    }

    return OnnxResult::Ok;
}

