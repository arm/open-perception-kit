#include "Inference.h"

#include "amp/String.h"
#include "amp/Result.h"
#include "gst/video/video-enumtypes.h"
#include "onnx/Tools.h"
#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"
#include "uniflow/detection_types.h"
#include "uniflow/model_io.h"
#include "uniflow/tensor_view.h"
#include <memory>

#include <fmt/core.h>

#include "amp/File.h"
#include "amp/String.h"
#include "amp/Result.h"

#include "ModelDescriptor.h"

using namespace onnx;

Inference::Inference() {
}

Inference::~Inference() {   
    if(this->session) delete this->session;
}

amp::Result<void> Inference::setupFromJson(const std::string& filePath) {

    auto descResult = ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{ descResult.error() };
    }

    { // setup model file name
        std::string modelRoot = filePath;
        if(amp::utf8::contains(modelRoot, '/')) {
            size_t lastSlashAt = amp::utf8::lastIndexOf(modelRoot, '/');
            modelRoot = amp::utf8::left(modelRoot, lastSlashAt + 1);
        } else {
            modelRoot = "";
        }
        (*descResult).modelFile = modelRoot + (*descResult).modelFile;
    }

    auto setupResult = setup(*descResult);
    if(!setupResult) {
        return tl::unexpected{ setupResult.error() };
    }

    return { };
}

amp::Result<void> Inference::setup(const ModelDescriptor& modelDesc_) {

    this->api = ApiTensorGlue();
    this->modelDescriptor = modelDesc_;
    //this->modelPath = file;

    try {
        
        this->sessionOptions = new Ort::SessionOptions();
        this->sessionOptions->SetIntraOpNumThreads(1);

        this->environment = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
        this->memoryInfo = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
  
        this->session = new Ort::Session(*this->environment, modelDescriptor.modelFile.c_str(), *this->sessionOptions);

        auto modelResult = onnx::Tools::inspectModel(*this->session);
        if(!modelResult) {
            return tl::unexpected{ modelResult.error() };
        }
        this->model = *modelResult;
        
        std::string modelLog = onnx::Tools::toString(this->model);
        printf("========= New  model  parsed =========\n");
        printf("%s", modelLog.c_str());
        printf("========= ================== =========\n");

        setupTensorsForModel();

        this->setupReady = true;
    } 
    catch (const std::exception& e) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::OnnxLowLevelError, e.what()));
    }

    return { };
}

void Inference::setupTensorsForModel() {

    for(size_t i = 0; i < this->model.modelInputCount; i++) {
        api.inputTensors[i] = std::make_unique<onnx::Tensor>(this->model.inputs[i].shape, this->model.inputs[i].valueType);
        api.inputNames.push_back(this->model.inputs[i].name.c_str());
        api.inputTensorVector.push_back(api.inputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

    for(size_t i = 0; i < this->model.modelOutputCount; i++) {
        api.outputTensors[i] = std::make_unique<onnx::Tensor>(this->model.outputs[i].shape, this->model.outputs[i].valueType);
        api.outputNames.push_back(this->model.outputs[i].name.c_str());
        api.outputTensorVector.push_back(api.outputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

}

onnx::Result Inference::preprocessImageData(size_t tensorIndex, const uint8_t* data, uflw::TensorDataKind dataKind, uflw::ValueType valueType, size_t imageWidth, size_t imageHeight) {

    uflw::NetworkInputBuilder::Setup setup;
    setup.original.data = data;
    setup.original.width = imageWidth;
    setup.original.height = imageHeight;
    setup.original.byteCount = imageWidth * imageHeight * 3;
    setup.original.kind = dataKind;
    setup.original.type = valueType;

    size_t modelWidth, modelHeight;
    model.inputs[tensorIndex].getImageWidthHeight(modelWidth, modelHeight);

    setup.target.data = api.inputTensors[tensorIndex]->getData();
    setup.target.byteCount = api.inputTensors[tensorIndex]->getByteCount();
    setup.target.width = modelWidth;
    setup.target.height = modelHeight;
    //setup.target.kind = uflw::TensorDataKind::ImageRgbChw;
    setup.target.kind = model.inputs[0].dataKind;
    setup.target.type = uflw::ValueType::f32;

    uflw::Result result = inputBuilder->build(setup);
    if(uflw::Result::Ok != result) {
      printf("ERROR!\n");
    }

    this->inferenceMetaData.image.width = imageWidth;
    this->inferenceMetaData.image.height = imageHeight;
    this->inferenceMetaData.image.modelWidth = modelWidth;
    this->inferenceMetaData.image.modelHeight = modelHeight;

    return onnx::Result::Ok;

}

onnx::Result Inference::inference() {

    if(api.inputTensorVector.size() > 1) {
        *(float*)api.inputTensors[0]->getData() = 0.99f; // confidence
        *(int64_t*)api.inputTensors[1]->getData() = 1; // numdetections
        *(float*)api.inputTensors[2]->getData() = 0.9f; // iou threshold
    }

    if(false == this->useDynamicOutput) {
    this->session->Run(
        Ort::RunOptions { nullptr },
        (const char* const*)this->api.inputNames.data(),
        api.inputTensorVector.data(),
        api.inputTensorVector.size(),
        (const char* const*)api.outputNames.data(),
        api.outputTensorVector.data(),
        api.outputTensorVector.size());
    } else {
        dynamicOutputData = this->session->Run(
    Ort::RunOptions{nullptr},
    (const char* const*)api.inputNames.data(),
    api.inputTensorVector.data(),
    api.inputTensorVector.size(),
    (const char* const*)api.outputNames.data(),
    api.outputNames.size());
    }

    return onnx::Result::Ok;

}

onnx::Result Inference::postprocess(const uflw::NetworkOutputParser::Settings& settings,
                                      uflw::DetectionResult& outDetectionResults) {

    if (!this->useDynamicOutput) {
        // --- STATIC, PREALLOCATED OUTPUTS ---

        const uflw::TensorReader* tensorReaders[4] = { nullptr, nullptr, nullptr, nullptr };

        for (size_t i = 0; i < 4; ++i) {
            if (i < model.modelOutputCount) {
                if (this->outputTensorReaders[i] == nullptr) {
                    this->outputTensorReaders[i] = std::make_unique<uflw::TensorReader>(
                        api.outputTensors[i]->getData(),
                        api.outputTensors[i]->getByteCount(),
                        model.outputs[i].shape,
                        model.outputs[i].valueType,
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
            std::min<size_t>(std::min<size_t>(model.modelOutputCount, dynamicOutputData.size()), 4);

        for (size_t i = 0; i < numOutputs; ++i) {
            Ort::Value& v = dynamicOutputData[i];

            auto tinfo = v.GetTensorTypeAndShapeInfo();
            auto onnxShape = tinfo.GetShape();
            ONNXTensorElementDataType elemType = tinfo.GetElementType();

            // Map ONNX type → uflw::ValueType
            uflw::ValueType valueType;
            if (!onnx::Tools::onnxTypeToUniflowType(elemType, valueType)) {
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

    return onnx::Result::Ok;
}

